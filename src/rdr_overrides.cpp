// Game overrides do Red Dead Revolver (SLUS-20500 v1.03).
// Aplicados pelo ps2xRuntime logo apos o loadELF, so para o ELF descomprimido
// (game/SLUS_205.00.unpacked.elf copiado como game/disc/SLUS_205.00, CRC32 EF570C27).
// Todo item aqui deve estar registrado em docs/FUNCOES.md.

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "runtime/ps2_mpeg_options.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

namespace
{
    constexpr uint32_t kRdrUnpackedElfCrc32 = 0xEF570C27u;

    // O jogo le o disco por LSN (sceCdRead/sceCdReadChain sem sceCdSearchFile), entao o HLE de CD
    // precisa da imagem do disco; o runtime nao tem opcao de linha de comando para isso.
    // Ordem: variavel RDR_CD_IMAGE; senao o primeiro .iso em <raiz do projeto>/ROM
    // (cdrom0: = game/disc, entao a raiz e dois niveis acima).
    std::filesystem::path findCdImage(const std::filesystem::path &elfDirectory)
    {
        if (const char *env = std::getenv("RDR_CD_IMAGE"); env && env[0] != '\0')
        {
            return std::filesystem::path(env);
        }

        std::error_code ec;
        const std::filesystem::path romDir = elfDirectory.parent_path().parent_path() / "ROM";
        for (const auto &entry : std::filesystem::directory_iterator(romDir, ec))
        {
            std::string ext = entry.path().extension().string();
            for (char &c : ext)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (entry.is_regular_file(ec) && ext == ".iso")
            {
                return entry.path();
            }
        }
        return {};
    }

    void applyRdrOverrides(PS2Runtime &runtime)
    {
        (void)runtime;

        // O laço de quadro do player de FMV (FUN_00236058) chama sceMpegGetPicture antes de
        // alimentar o SPU2 (FUN_00470540); com o GetPicture bloqueante do HLE, a thread leitora
        // ficava presa no callback de áudio esperando espaço no anel (deadlock). FUNCOES.md #11.
        ps2_stubs::setMpegGetPictureNonBlocking(true);
        // A thread leitora (FUN_004713f0) ignora o retorno do sceMpegDemuxPss: com backpressure o HLE
        // devolvia 0 consumido e o resto do video era descartado. FUNCOES.md #12.
        ps2_stubs::setMpegDemuxBlockOnBackpressure(true);
        // O jogo le o .PSS com sceCdRead (sem sceCdSt): o fim do video vem do codigo de fim de programa. FUNCOES.md #14.
        ps2_stubs::setMpegProgramEndIsEof(true);
        PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();
        const std::filesystem::path image = findCdImage(paths.elfDirectory);
        if (image.empty())
        {
            std::cerr << "[rdr] imagem do disco nao encontrada (defina RDR_CD_IMAGE ou ponha a ISO em ROM/)" << std::endl;
            return;
        }
        paths.cdImage = image;
        PS2Runtime::setIoPaths(paths);
        std::cerr << "[rdr] CD image: " << PS2Runtime::getIoPaths().cdImage.string() << std::endl;
    }
}

PS2_REGISTER_GAME_OVERRIDE("Red Dead Revolver (SLUS-20500)", "SLUS_205.00", 0u, kRdrUnpackedElfCrc32, applyRdrOverrides)
