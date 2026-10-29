#pragma once

#include "Project.h"

#include <memory>
#include <string>
#include <string_view>

namespace jackal
{

constexpr int kProjectFormatVersion = 4;

struct ParseResult
{
    bool ok = false;
    std::unique_ptr<Project> project;   // heap: a Project is ~200 KB
    std::string error;
    int migratedFrom = 0;               // older version that was read (0 = current)
};

std::string toJson (const Project&);
ParseResult fromJson (std::string_view text);   // never throws

} // namespace jackal
