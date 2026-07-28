#include "TextureCatalog.h"

#include <algorithm>
#include <filesystem>

namespace
{
    // Both spellings ("assets/textures/x.png" from .rtsdata and "textures/x.png"
    // used internally) plus either separator collapse to one key.
    std::string NormalizeKey(const std::string& path)
    {
        std::string key;
        key.reserve(path.size());
        for (char c : path)
            key += (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        constexpr const char* prefix = "assets/";
        if (key.rfind(prefix, 0) == 0)
            key.erase(0, std::string_view(prefix).size());
        while (!key.empty() && key.front() == '/')
            key.erase(0, 1);

        return key;
    }

    bool IsImageExtension(const std::filesystem::path& path)
    {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp";
    }
}

TextureCatalog::~TextureCatalog()
{
    for (auto& asset : assets)
    {
        if (asset.loaded)
            UnloadTexture(asset.texture);
    }
}

void TextureCatalog::ScanAndLoad(const std::string& assetsDir)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path root(assetsDir);
    if (!fs::exists(root, ec))
        return;

    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(root, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec))
    {
        if (ec)
            break;
        if (it->is_regular_file(ec) && IsImageExtension(it->path()))
            files.push_back(it->path());
    }

    // Alphabetical so the list is stable between runs regardless of what the
    // filesystem hands back.
    std::sort(files.begin(), files.end());

    for (const auto& file : files)
    {
        TextureAsset asset;
        asset.absolutePath = file.string();
        asset.relativePath = fs::relative(file, root, ec).generic_string();
        asset.gamePath = "assets/" + asset.relativePath;

        fs::path parent = fs::path(asset.relativePath).parent_path();
        asset.group = parent.empty() ? std::string("<root>") : parent.generic_string();

        asset.fileSize = static_cast<long long>(fs::file_size(file, ec));
        if (ec)
            asset.fileSize = 0;

        asset.texture = LoadTexture(asset.absolutePath.c_str());
        asset.loaded = asset.texture.id != 0;
        asset.width = asset.texture.width;
        asset.height = asset.texture.height;

        if (std::find(groups.begin(), groups.end(), asset.group) == groups.end())
            groups.push_back(asset.group);

        assets.push_back(std::move(asset));
    }
}

TextureAsset* TextureCatalog::FindMutable(const std::string& path)
{
    std::string key = NormalizeKey(path);
    for (auto& asset : assets)
    {
        if (NormalizeKey(asset.relativePath) == key)
            return &asset;
    }
    return nullptr;
}

const TextureAsset* TextureCatalog::Find(const std::string& path) const
{
    return const_cast<TextureCatalog*>(this)->FindMutable(path);
}

Texture2D TextureCatalog::TextureFor(const std::string& path) const
{
    const TextureAsset* asset = Find(path);
    return asset != nullptr ? asset->texture : Texture2D{};
}

void TextureCatalog::MarkBound(const std::string& path, const std::string& slotCode)
{
    TextureAsset* asset = FindMutable(path);
    if (asset == nullptr)
        return;

    if (std::find(asset->boundTo.begin(), asset->boundTo.end(), slotCode) == asset->boundTo.end())
        asset->boundTo.push_back(slotCode);
}

int TextureCatalog::LoadedCount() const
{
    return static_cast<int>(std::count_if(assets.begin(), assets.end(),
                                          [](const TextureAsset& a) { return a.loaded; }));
}

int TextureCatalog::UnboundCount() const
{
    return static_cast<int>(std::count_if(assets.begin(), assets.end(),
                                          [](const TextureAsset& a) { return a.boundTo.empty(); }));
}
