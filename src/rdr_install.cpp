// Instalador da versao de distribuicao (docs/LAYOUT.md): le a ISO do usuario (SLUS-20500), copia os arquivos
// para pastas por tipo (como o GTA Vice City), desempacota o ELF de boot e grava system/disc.vdisc, o disco
// virtual com os mesmos setores da ISO (ps2_virtual_disc.h). Nada do jogo vai junto com a distribuicao.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using RdrInstallProgress = std::function<void(uint64_t done, uint64_t total, const std::string &label)>;

namespace
{
    constexpr uint32_t kSector = 2048u;
    constexpr uint32_t kUnpackedElfCrc32 = 0xEF570C27u; // mesmo CRC do registro do override (rdr_overrides.cpp)

    struct IsoFile
    {
        std::string path; // caminho no disco, com '/', sem ";1"
        uint32_t lbn = 0, size = 0;
    };

    struct IsoReader
    {
        std::ifstream in;
        bool sector(uint32_t lbn, uint32_t count, std::vector<uint8_t> &out)
        {
            out.assign(static_cast<size_t>(count) * kSector, 0);
            in.clear();
            in.seekg(static_cast<std::streamoff>(lbn) * kSector);
            in.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(out.size()));
            return in.gcount() == static_cast<std::streamsize>(out.size());
        }
    };

    uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

    bool walkDir(IsoReader &iso, uint32_t lbn, uint32_t size, const std::string &prefix, std::vector<IsoFile> &files,
                 std::vector<std::pair<uint32_t, uint32_t>> &dirs, int depth)
    {
        if (depth > 8)
            return false;
        dirs.emplace_back(lbn, size);
        std::vector<uint8_t> buf;
        if (!iso.sector(lbn, (size + kSector - 1) / kSector, buf))
            return false;
        for (size_t pos = 0; pos < size;)
        {
            const uint8_t len = buf[pos];
            if (len == 0) // resto do setor vazio
            {
                pos = (pos / kSector + 1) * kSector;
                continue;
            }
            const uint8_t *r = &buf[pos];
            const uint32_t extent = le32(r + 2), bytes = le32(r + 10);
            const bool isDir = (r[25] & 2) != 0;
            const uint8_t nameLen = r[32];
            std::string name(reinterpret_cast<const char *>(r + 33), nameLen);
            pos += len;
            if (nameLen == 1 && (name[0] == 0 || name[0] == 1))
                continue;
            if (const size_t semi = name.find(';'); semi != std::string::npos)
                name.resize(semi);
            const std::string path = prefix.empty() ? name : prefix + "/" + name;
            if (isDir)
            {
                if (!walkDir(iso, extent, bytes, path, files, dirs, depth + 1))
                    return false;
            }
            else
                files.push_back({path, extent, bytes});
        }
        return true;
    }

    std::string upper(std::string s)
    {
        for (char &c : s)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }

    // Onde cada arquivo do disco fica na instalacao (docs/LAYOUT.md). O resto vai para system/ com o mesmo
    // caminho do disco (cdrom0: aponta para system/: SYSTEM.CNF, SYSTEM/*.IRX, IOPRP).
    std::string layoutPath(const std::string &discPath)
    {
        const std::string u = upper(discPath);
        const std::string leaf = discPath.substr(discPath.find_last_of('/') + 1);
        auto under = [&](const char *dir) { return u.rfind(dir, 0) == 0; };
        if (under("PSS/"))
        {
            // .mpg como no GTA Vice City: o .PSS ja e um MPEG Program Stream, o nome muda e os bytes nao
            return "movies/" + leaf.substr(0, leaf.find_last_of('.')) + ".mpg";
        }
        if (u == "STREAMS.DAT" || u == "BANKS.DAT")
            return "audio/" + leaf;
        if (u == "SWAT.DAT")
            return "data/" + leaf;
        if (u == "FRONT.DAT")
            return "frontend/" + leaf;
        if (u == "PUPSHOW.DAT")
            return "extras/" + leaf;
        if (under("LVLS_SNG/"))
            return "levels/single/" + leaf;
        if (under("LVLS_M2/"))
            return "levels/mp2/" + leaf;
        if (under("LVLS_M4/"))
            return "levels/mp4/" + leaf;
        if (u == "SLUS_205.00")
            return "system/SLUS_205.00.packed"; // original do disco; system/SLUS_205.00 e o desempacotado
        return "system/" + discPath;
    }

    uint32_t crc32(const std::vector<uint8_t> &data)
    {
        uint32_t table[256];
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        uint32_t crc = 0xFFFFFFFFu;
        for (uint8_t b : data)
            crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }

    // Desempacotador do ELF de boot (porte de tools/rdr_unpack.py): aPLib com XOR de chave crescente.
    constexpr uint32_t kPackedOffset = 0x1000 + 0x600, kLoadAddr = 0x00100000, kEntry = 0x00100008;
    constexpr uint32_t kTextEnd = 0x004FEA80, kBssEnd = 0x007A26CC;

    struct Depacker
    {
        const std::vector<uint8_t> &d;
        size_t p;
        uint8_t key = 1, tag = 0, bits = 0;
        uint8_t byte()
        {
            if (p >= d.size())
                throw std::runtime_error("packed ELF truncated");
            const uint8_t b = d[p++] ^ key;
            ++key;
            return b;
        }
        int bit()
        {
            --bits;
            if (bits == 0xFF)
            {
                tag = byte();
                bits = 7;
            }
            const int b = tag >> 7;
            tag = static_cast<uint8_t>(tag << 1);
            return b;
        }
        uint32_t gamma()
        {
            uint32_t v = 1;
            do
                v = (v << 1) + bit();
            while (bit());
            return v;
        }
    };

    std::vector<uint8_t> depack(const std::vector<uint8_t> &data)
    {
        Depacker s{data, kPackedOffset};
        std::vector<uint8_t> out{data.at(kPackedOffset)};
        s.p += 1;
        bool lwm = false;
        uint32_t r0 = 0;
        auto copy = [&](uint32_t off, uint32_t n) {
            if (off == 0 || off > out.size())
                throw std::runtime_error("packed ELF corrupt");
            for (uint32_t i = 0; i < n; ++i)
                out.push_back(out[out.size() - off]);
        };
        for (;;)
        {
            if (!s.bit())
            {
                out.push_back(s.byte());
                lwm = false;
            }
            else if (!s.bit())
            {
                uint32_t hi = s.gamma();
                if (!lwm && hi == 2)
                    copy(r0, s.gamma());
                else
                {
                    hi -= lwm ? 2 : 3;
                    const uint32_t off = (hi << 8) + s.byte();
                    uint32_t n = s.gamma();
                    if (off >= 32000)
                        ++n;
                    if (off >= 1280)
                        ++n;
                    if (off < 128)
                        n += 2;
                    copy(off, n);
                    r0 = off;
                }
                lwm = true;
            }
            else if (!s.bit())
            {
                const uint8_t b = s.byte();
                const uint32_t n = 2 + (b & 1), off = b >> 1;
                if (off == 0)
                    break;
                copy(off, n);
                r0 = off;
                lwm = true;
            }
            else
            {
                uint32_t off = 0;
                for (int i = 0; i < 4; ++i)
                    off = (off << 1) + s.bit();
                out.push_back(off ? out[out.size() - off] : 0);
                lwm = false;
            }
        }
        return out;
    }

    void put16(std::vector<uint8_t> &v, uint32_t x) { v.push_back(x & 0xFF), v.push_back((x >> 8) & 0xFF); }
    void put32(std::vector<uint8_t> &v, uint32_t x) { put16(v, x & 0xFFFF), put16(v, x >> 16); }

    std::vector<uint8_t> buildElf(const std::vector<uint8_t> &img)
    {
        const uint32_t segOff = 0x1000, textSize = kTextEnd - kLoadAddr, dataOff = segOff + textSize;
        const uint32_t dataSize = static_cast<uint32_t>(img.size()) - textSize;
        const uint32_t bssAddr = kLoadAddr + static_cast<uint32_t>(img.size());
        const std::string shstr("\0.text\0.data\0.bss\0.shstrtab\0", 28);
        const uint32_t shstrOff = segOff + static_cast<uint32_t>(img.size());
        const uint32_t shOff = (shstrOff + static_cast<uint32_t>(shstr.size()) + 3) & ~3u;
        std::vector<uint8_t> out = {0x7F, 'E', 'L', 'F', 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        put16(out, 2), put16(out, 8), put32(out, 1), put32(out, kEntry), put32(out, 52), put32(out, shOff);
        put32(out, 0x20924001), put16(out, 52), put16(out, 32), put16(out, 2), put16(out, 40), put16(out, 5), put16(out, 4);
        for (uint32_t x : {1u, segOff, kLoadAddr, kLoadAddr, textSize, textSize, 5u, 0x10u})
            put32(out, x);
        for (uint32_t x : {1u, dataOff, kTextEnd, kTextEnd, dataSize, kBssEnd - kTextEnd, 6u, 0x10u})
            put32(out, x);
        out.resize(segOff, 0);
        out.insert(out.end(), img.begin(), img.end());
        out.insert(out.end(), shstr.begin(), shstr.end());
        out.resize(shOff, 0);
        auto sh = [&](uint32_t name, uint32_t typ, uint32_t flags, uint32_t addr, uint32_t off, uint32_t size, uint32_t align) {
            for (uint32_t x : {name, typ, flags, addr, off, size, 0u, 0u, align, 0u})
                put32(out, x);
        };
        sh(0, 0, 0, 0, 0, 0, 0);
        sh(1, 1, 6, kLoadAddr, segOff, textSize, 16);
        sh(7, 1, 3, kTextEnd, dataOff, dataSize, 16);
        sh(13, 8, 3, bssAddr, shstrOff, kBssEnd - bssAddr, 16);
        sh(18, 3, 0, 0, shstrOff, static_cast<uint32_t>(shstr.size()), 1);
        return out;
    }

    bool writeFile(const fs::path &p, const std::vector<uint8_t> &data)
    {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        std::ofstream o(p, std::ios::binary);
        o.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
        return static_cast<bool>(o);
    }
}

// Instalacoes antigas tem movies/*.PSS: renomeia e atualiza o disc.vdisc (texto: LSN, tamanho, caminho)
static void migrateMovies(const fs::path &root)
{
    const fs::path file = root / "system" / "disc.vdisc";
    std::ifstream in(file, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    bool changed = false;
    std::error_code ec;
    for (size_t pos = text.find("../movies/"); pos != std::string::npos; pos = text.find("../movies/", pos + 1u))
    {
        const size_t end = text.find('\n', pos);
        if (end == std::string::npos || end < pos + 4u || upper(text.substr(end - 4u, 4u)) != ".PSS")
            continue;
        const std::string oldRel = text.substr(pos + 3u, end - pos - 3u);
        const std::string newRel = oldRel.substr(0, oldRel.size() - 4u) + ".mpg";
        fs::rename(root / fs::u8path(oldRel), root / fs::u8path(newRel), ec);
        if (ec)
            return; // o resto fica para a proxima abertura; o disc.vdisc so muda com o arquivo no lugar
        text.replace(pos + 3u, oldRel.size(), newRel);
        changed = true;
        std::ofstream(file, std::ios::binary) << text;
    }
    (void)changed;
}

bool rdrIsInstalled(const fs::path &root)
{
    std::error_code ec;
    const bool installed = fs::exists(root / "system" / "disc.vdisc", ec) && fs::exists(root / "system" / "SLUS_205.00", ec);
    static bool s_migrated = false;
    if (installed && !s_migrated)
    {
        s_migrated = true;
        migrateMovies(root);
    }
    return installed;
}

// Instala a partir da ISO. Retorna "" se deu certo, senao a mensagem de erro (em ingles, mostrada no lancador).
std::string rdrInstallFromIso(const fs::path &isoPath, const fs::path &root, const RdrInstallProgress &progress)
{
    try
    {
        IsoReader iso;
        iso.in.open(isoPath, std::ios::binary);
        if (!iso.in)
            return "Could not open the disc image.";
        std::vector<uint8_t> pvd;
        if (!iso.sector(16, 1, pvd) || pvd[0] != 1 || std::memcmp(&pvd[1], "CD001", 5) != 0)
            return "This file is not a PS2 DVD image (ISO9660).";
        const uint32_t totalSectors = le32(&pvd[80]);
        std::vector<IsoFile> files;
        std::vector<std::pair<uint32_t, uint32_t>> dirs;
        if (!walkDir(iso, le32(&pvd[156 + 2]), le32(&pvd[156 + 10]), "", files, dirs, 0))
            return "Could not read the disc directory.";
        auto elfIt = std::find_if(files.begin(), files.end(), [](const IsoFile &f) { return upper(f.path) == "SLUS_205.00"; });
        if (elfIt == files.end())
            return "This is not Red Dead Revolver (USA, SLUS-20500).";

        // 1) ELF de boot: desempacota e confere o CRC antes de copiar 2 GB
        std::vector<uint8_t> packed;
        iso.sector(elfIt->lbn, (elfIt->size + kSector - 1) / kSector, packed);
        packed.resize(elfIt->size);
        const std::vector<uint8_t> elf = buildElf(depack(packed));
        if (crc32(elf) != kUnpackedElfCrc32)
            return "Unsupported version of the game (needs Red Dead Revolver USA, SLUS-20500).";
        if (!writeFile(root / "system" / "SLUS_205.00", elf))
            return "Could not write to the game folder (check permissions).";

        // 2) Arquivos do disco
        uint64_t total = 0, done = 0;
        for (const IsoFile &f : files)
            total += f.size;
        std::vector<char> buf(8u << 20);
        std::string vdisc = "VDISC1 " + std::to_string(totalSectors) + "\n";
        for (const IsoFile &f : files)
        {
            const std::string rel = layoutPath(f.path);
            const fs::path dst = root / fs::u8path(rel);
            progress(done, total, f.path);
            std::error_code ec;
            fs::create_directories(dst.parent_path(), ec);
            std::ofstream o(dst, std::ios::binary);
            if (!o)
                return "Could not write " + rel + " (check permissions and free space).";
            iso.in.clear();
            iso.in.seekg(static_cast<std::streamoff>(f.lbn) * kSector);
            for (uint64_t left = f.size; left > 0;)
            {
                const size_t n = static_cast<size_t>(std::min<uint64_t>(left, buf.size()));
                iso.in.read(buf.data(), static_cast<std::streamsize>(n));
                if (iso.in.gcount() != static_cast<std::streamsize>(n))
                    return "The disc image is truncated (" + f.path + ").";
                o.write(buf.data(), static_cast<std::streamsize>(n));
                if (!o)
                    return "Not enough free space to install the game.";
                left -= n;
                done += n;
                progress(done, total, f.path);
            }
            vdisc += std::to_string(f.lbn) + " " + std::to_string(f.size) + " ../" + rel + "\n";
        }

        // 3) Setores sem arquivo que o jogo le: area de sistema, descritores e diretorios (o motor procura os
        // arquivos lendo o diretorio ISO9660 por LSN)
        uint32_t firstFile = totalSectors;
        for (const IsoFile &f : files)
            firstFile = std::min(firstFile, f.lbn);
        std::vector<uint8_t> head;
        iso.sector(0, firstFile, head);
        if (!writeFile(root / "system" / "disc" / "header.bin", head))
            return "Could not write the disc index.";
        vdisc += "0 " + std::to_string(head.size()) + " disc/header.bin\n";
        for (const auto &[lbn, size] : dirs)
        {
            if (lbn < firstFile)
                continue;
            std::vector<uint8_t> d;
            iso.sector(lbn, (size + kSector - 1) / kSector, d);
            const std::string rel = "disc/dir_" + std::to_string(lbn) + ".bin";
            writeFile(root / "system" / fs::u8path(rel), d);
            vdisc += std::to_string(lbn) + " " + std::to_string(d.size()) + " " + rel + "\n";
        }
        std::ofstream v(root / "system" / "disc.vdisc", std::ios::binary);
        v << vdisc;
        if (!v)
            return "Could not write the disc index.";
        progress(total, total, "");
        return {};
    }
    catch (const std::exception &e)
    {
        return std::string("Install failed: ") + e.what();
    }
}
