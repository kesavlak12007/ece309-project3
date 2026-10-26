// src/tools/workspace.cpp — lexical path validation (spec §4.4).
// The provided workspace_provided.cpp handles filesystem-based symlink checks.

#include "tools/workspace.h"

#include <string>

namespace fs = std::filesystem;

bool Workspace::resolve(std::string_view user_path, fs::path* out,
                        std::string* reason) const {
    // The public interface guarantees that out and reason are non-null.
    try {
        if (user_path.empty()) {
            *reason = "empty path";
            return false;
        }
        if (user_path.find('\0') != std::string_view::npos) {
            *reason = "path contains NUL byte";
            return false;
        }

        const fs::path path{std::string(user_path)};
        if (path.is_absolute() || path.has_root_path()) {
            *reason = "absolute paths are not allowed";
            return false;
        }

        const fs::path normalized = path.lexically_normal();
        if (normalized.begin() != normalized.end() &&
            *normalized.begin() == "..") {
            *reason = "path escapes the workspace";
            return false;
        }

        *out = normalized == "." ? root() : root() / normalized;
        return true;
    } catch (...) {
        // Even assigning an error message can allocate. Under total memory
        // exhaustion, a nonempty diagnostic cannot be guaranteed.
        try {
            *reason = "invalid path";
        } catch (...) {
        }
        return false;
    }
}
