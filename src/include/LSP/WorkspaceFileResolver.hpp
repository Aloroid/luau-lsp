#pragma once
#include <optional>
#include <filesystem>
#include <unordered_map>
#include <utility>
#include "Luau/FileResolver.h"
#include "Luau/StringUtils.h"
#include "Luau/Config.h"
#include "LSP/Client.hpp"
#include "LSP/Uri.hpp"
#include "LSP/Sourcemap.hpp"
#include "LSP/TextDocument.hpp"


// A wrapper around a text document pointer
// A text document might be temporarily created for the purposes of this function
// in which case it should be deleted once the ptr goes out of scope.
// I don't think we can use a unique_ptr here, because a managed text document is not owned
// NOTE: document may still be nil!
struct TextDocumentPtr
{
private:
    const TextDocument* document = nullptr;
    bool isTemporary = false;

public:
    explicit TextDocumentPtr(const TextDocument* document)
        : document(document)
    {
    }

    explicit TextDocumentPtr(const lsp::DocumentUri& uri, const std::string& languageId, const std::string& content)
        : document(new TextDocument(uri, languageId, 0, content))
        , isTemporary(true)
    {
    }

    explicit operator bool() const
    {
        return document != nullptr;
    }

    const TextDocument* operator->() const
    {
        return document;
    }

    ~TextDocumentPtr()
    {
        if (isTemporary)
            delete document;
    }

    TextDocumentPtr(const TextDocumentPtr& other) = delete;
    TextDocumentPtr& operator=(const TextDocumentPtr& other) = delete;

    TextDocumentPtr(TextDocumentPtr&& other) noexcept
        : document(std::exchange(other.document, nullptr))
        , isTemporary(std::exchange(other.isTemporary, false))
    {
    }

    TextDocumentPtr& operator=(TextDocumentPtr&& other) noexcept
    {
        std::swap(document, other.document);
        std::swap(isTemporary, other.isTemporary);
        return *this;
    }
};

std::optional<std::filesystem::path> resolveDirectoryAlias(
    const std::filesystem::path& rootPath, const std::unordered_map<std::string, std::string>& directoryAliases, const std::string& str);

struct WorkspaceFileResolver
    : Luau::FileResolver
    , Luau::ConfigResolver
{
    Luau::Config defaultConfig;
    std::shared_ptr<BaseClient> client;

    // The root source node from a parsed Rojo source map
    Uri rootUri;
    SourceNodePtr rootSourceNode;
    mutable std::unordered_map<std::string, SourceNodePtr> realPathsToSourceNodes{};
    mutable std::unordered_map<Luau::ModuleName, SourceNodePtr> virtualPathsToSourceNodes{};

    // Plugin-provided DataModel information
    PluginNodePtr pluginInfo;

    // Currently opened files where content is managed by client
    mutable std::unordered_map</* DocumentUri */ std::string, TextDocument> managedFiles{};
    mutable std::unordered_map<std::string, Luau::Config> configCache{};

    WorkspaceFileResolver()
    {
        defaultConfig.mode = Luau::Mode::Nonstrict;
    }

    // Create a WorkspaceFileResolver with a specific default configuration
    explicit WorkspaceFileResolver(Luau::Config defaultConfig)
        : defaultConfig(std::move(defaultConfig)){};

    // Handle normalisation to simplify lookup
    static std::string normalisedUriString(const lsp::DocumentUri& uri);

    /// The file is managed by the client, so FS will be out of date
    const TextDocument* getTextDocument(const lsp::DocumentUri& uri) const;
    const TextDocument* getTextDocumentFromModuleName(const Luau::ModuleName& name) const;

    TextDocumentPtr getOrCreateTextDocumentFromModuleName(const Luau::ModuleName& name);

    /// The name points to a virtual path (i.e., game/ or ProjectRoot/)
    static bool isVirtualPath(const Luau::ModuleName& name)
    {
        return name == "game" || name == "ProjectRoot" || Luau::startsWith(name, "game/") || Luau::startsWith(name, "ProjectRoot/");
    }

    std::filesystem::path getRequireBasePath(std::optional<Luau::ModuleName> fileModuleName) const;

    // Return the corresponding module name from a file Uri
    // We first try and find a virtual file path which matches it, and return that. Otherwise, we use the file system path
    Luau::ModuleName getModuleName(const Uri& name) const;

    std::optional<SourceNodePtr> getSourceNodeFromVirtualPath(const Luau::ModuleName& name) const;

    std::optional<SourceNodePtr> getSourceNodeFromRealPath(const std::string& name) const;

    std::optional<std::filesystem::path> getRealPathFromSourceNode(const SourceNodePtr& sourceNode) const;
    static Luau::ModuleName getVirtualPathFromSourceNode(const SourceNodePtr& sourceNode);

    std::optional<Luau::ModuleName> resolveToVirtualPath(const std::string& name) const;

    std::optional<std::filesystem::path> resolveToRealPath(const Luau::ModuleName& name) const;

    std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name) override;

    std::optional<Luau::ModuleInfo> resolveStringRequire(const Luau::ModuleInfo* context, const std::string& requiredString) const;
    std::optional<Luau::ModuleInfo> resolveModule(const Luau::ModuleInfo* context, Luau::AstExpr* node) override;

    std::string getHumanReadableModuleName(const Luau::ModuleName& name) const override;

    const Luau::Config& getConfig(const Luau::ModuleName& name) const override;

    const Luau::Config& readConfigRec(const std::filesystem::path& path) const;

    void clearConfigCache();

    void writePathsToMap(const SourceNodePtr& node, const std::string& base);

    void updateSourceMap(const std::string& sourceMapContents);
};
