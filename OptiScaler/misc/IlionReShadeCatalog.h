#pragma once

// ILION: catalogue of ReShade effects to install from inside the game. ReShade stays the effects engine: ILION only
// downloads the files of the effect the user picks, puts them where ReShade looks for them and asks ReShade to load
// them (through the bridge, misc/IlionReShade.h).
//
//   index        the official package list ReShade's own installer uses (crosire/reshade-shaders, branch "list",
//                EffectPackages.ini), read only when the user asks for the catalogue
//   one effect   its .fx, the headers it includes and the textures it names, resolved in its repository's file tree
//                and downloaded one by one from GitHub; nothing else of the package is downloaded
//   where        the package's own InstallPath / TextureInstallPath under the game folder, as ReShade's installer
//   ownership    ILION-ReShade-efectos.txt in the game folder lists the files ILION wrote for each effect; a file that
//                was already there is never overwritten nor deleted, a file shared by two effects stays until both go
//
// Every network and disk operation runs on one background thread, never on the render thread; the menu only reads a
// snapshot.

#include <cstdint>
#include <string>
#include <vector>

namespace IlionReShadeCatalog
{
struct Package
{
    std::string name;
    std::string description;
    std::string repositoryUrl;
    std::vector<std::string> effects; // effect files the package offers
};

// State of one effect in the catalogue
struct EffectState
{
    enum Kind
    {
        Available,  // not on disk
        OnDisk,     // present in the shaders folder (installed by ILION, by Instalar_ReShade.bat or by the user)
        Working,    // being downloaded
        Failed,
    } kind = Available;
    bool byIlion = false;      // installed by this catalogue (can be removed by it)
    bool needsDepth = false;   // reads the game's depth buffer: may not work in every game
    std::string message;       // last error, a fixed English text (translated by the menu)
    std::string detail;        // what it is about: file names, HTTP status (not translated)
};

struct Snapshot
{
    enum Index
    {
        NotLoaded,
        Loading,
        Ready,
        IndexFailed,
    } index = NotLoaded;
    std::string indexError;
    std::vector<Package> packages;
    bool busy = false;     // the index, an install or a removal is running
    bool removing = false; // busyWith is being removed, not installed
    std::string busyWith;  // effect file being installed or removed
};

// An install or removal that finished
struct Finished
{
    std::string effect;
    bool installed = false; // false: removed
    bool changed = false;   // files were written or deleted: ReShade has to load its effects again
};

// Downloads the package index (once; again only when asked)
void LoadIndex();

// Grows with every change of the index or of an effect's state: the menu copies the snapshot only then
uint64_t Revision();

Snapshot Get();

// State of an effect file (case-insensitive)
EffectState StateOf(const std::string& effectFile);

// Installs one effect of a package with its dependencies. Returns false when another download is running.
bool Install(size_t packageIndex, const std::string& effectFile);

// Removes, in the background, the files ILION installed for an effect (shared and pre-existing files stay).
// Returns false when another operation is running.
bool Remove(const std::string& effectFile);

// Installs and removals finished since the last call (for the menu to update its panel and reload ReShade)
std::vector<Finished> TakeFinished();
} // namespace IlionReShadeCatalog
