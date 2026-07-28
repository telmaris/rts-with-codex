#ifndef TEXTURE_CATALOG_H
#define TEXTURE_CATALOG_H

// Every image file under assets/, loaded once and owned here.
//
// The catalog is deliberately dumb about meaning: it knows what is on disk, not
// what the game does with it. GameBindings.h supplies the other half (which game
// slot points at which file), and the two are joined through `boundTo`.

#include "raylib.h"

#include <string>
#include <vector>

struct TextureAsset
{
    // "textures/terrain/grass1.png" — relative to assets/, the stable identity.
    std::string relativePath;
    // "assets/textures/terrain/grass1.png" — the spelling buildings.rtsdata uses.
    std::string gamePath;
    std::string absolutePath;
    // Directory group used for filtering ("textures/terrain", "ui/menu", ...).
    std::string group;

    Texture2D texture{};
    int width{0};
    int height{0};
    long long fileSize{0};
    bool loaded{false};

    // Slot codes that reference this file, e.g. "building:Foundry". Empty means
    // nothing in the game points here — the file ships but is never drawn.
    std::vector<std::string> boundTo;
};

class TextureCatalog
{
public:
    ~TextureCatalog();

    // Recursively scans `assetsDir` for images and uploads them. Requires an
    // initialized window (raylib refuses to create textures without a context).
    void ScanAndLoad(const std::string& assetsDir);

    const std::vector<TextureAsset>& Assets() const { return assets; }
    // Every distinct `group`, in scan order.
    const std::vector<std::string>& Groups() const { return groups; }

    // Accepts either spelling ("assets/textures/x.png" or "textures/x.png") and
    // normalizes separators. Returns nullptr when the file is not on disk —
    // which is itself a finding, so callers report it rather than substituting.
    const TextureAsset* Find(const std::string& path) const;
    // Convenience for draw code: an unloaded/missing file yields a zero texture,
    // and raylib treats `id == 0` as "nothing to draw".
    Texture2D TextureFor(const std::string& path) const;

    // Records that `slotCode` draws `path`. Unknown paths are ignored (Find
    // already reports them), so a stale path in .rtsdata cannot crash the tool.
    void MarkBound(const std::string& path, const std::string& slotCode);

    int LoadedCount() const;
    int UnboundCount() const;

private:
    TextureAsset* FindMutable(const std::string& path);

    std::vector<TextureAsset> assets;
    std::vector<std::string> groups;
};

#endif
