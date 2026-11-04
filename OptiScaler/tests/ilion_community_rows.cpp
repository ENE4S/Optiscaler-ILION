// ILION COMMUNITY — imprime las filas que el cliente envía al publicar (ToServerRow), una por implementación de FG.
// Sirve para comprobar contra el servidor real que el esquema v2 admite exactamente lo que publica Community5.
// Compilar: cl /std:c++latest /EHsc /utf-8 /I external\nlohmann /I OptiScaler OptiScaler\tests\ilion_community_rows.cpp shell32.lib ole32.lib crypt32.lib

#include "misc/IlionCommunity.h"

#include <cstdio>

namespace C = IlionCommunity;

int main()
{
    C::Json rows = C::Json::array();
    int i = 0;
    for (const auto& f : C::kFgImpls)
    {
        const std::string id = f.id;
        if (id == "dlssg_salida")
            continue;
        for (const char* arch : { "rtx30", "rtx40", "rtx50", "rdna3", "arc_b" })
        {
            if (id != "ninguno" && C::FgMaxMultiplier(id, arch) == 0)
                continue;
            C::Profile p;
            p.id = C::NewLocalId();
            p.author = "Prueba";
            p.name = "Fila " + std::to_string(++i);
            p.gameExe = "dawnwalker.exe";
            p.gameName = "Dawnwalker";
            p.api = "DX12";
            p.arch = arch;
            p.gpu = std::string(arch).rfind("rtx", 0) == 0 ? "NVIDIA GeForce RTX" : std::string(arch) == "rdna3" ? "AMD Radeon RX 7900 XTX" : "Intel Arc B580";
            p.outW = 2560;
            p.outH = 1440;
            p.mode = "optimo";
            p.tech = std::string(arch).rfind("rtx", 0) == 0 ? "dlss" : std::string(arch) == "rdna3" ? "fsr4_driver" : "xess";
            p.model = p.tech == "dlss" ? "dlss_k" : p.tech == "fsr4_driver" ? "completo" : "no_aplica";
            p.preset = "calidad";
            p.fg = id;
            p.fgRoute = id == "ninguno" ? "ninguno" : C::DefaultRoute(id);
            p.multiplier = id == "ninguno" ? 0 : C::FgMaxMultiplier(id, arch); // el máximo admitido: el caso más exigente
            std::string err;
            if (!C::Validate(p, &err))
            {
                fprintf(stderr, "no válido %s/%s: %s\n", id.c_str(), arch, err.c_str());
                continue;
            }
            rows.push_back(C::ToServerRow(p));
        }
    }
    printf("%s\n", rows.dump().c_str());
    fprintf(stderr, "%d filas\n", (int) rows.size());
    return 0;
}
