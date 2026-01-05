// SPDX-FileCopyrightText: 2025 (c) David Andrs <andrsd@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <fmt/core.h>
#include <string>
#include <algorithm>

enum class ElementType {
    //
    POINT1,
    SEGMENT2,
    TRI3,
    QUAD4,
    TET4,
    HEX8,
    PRISM6,
    PYRAMID5
};

/// Convert string representation of an element type into enum
inline ElementType
element_type(std::string_view str)
{
    if (str == "BAR2")
        return ElementType::SEGMENT2;
    else if (str == "TRI" || str == "TRI3")
        return ElementType::TRI3;
    else if (str == "QUAD" || str == "QUAD4")
        return ElementType::QUAD4;
    else if (str == "TETRA" || str == "TET4")
        return ElementType::TET4;
    else if (str == "HEX" || str == "HEX8")
        return ElementType::HEX8;
    else
        throw std::runtime_error(fmt::format("Unsupported element type {}", str));
}

inline const char *
element_type_str(ElementType et)
{
    if (et == ElementType::POINT1)
        return "POINT";
    else if (et == ElementType::SEGMENT2)
        return "BAR2";
    else if (et == ElementType::TRI3)
        return "TRI3";
    else if (et == ElementType::QUAD4)
        return "QUAD4";
    else if (et == ElementType::TET4)
        return "TET4";
    else if (et == ElementType::HEX8)
        return "HEX8";
    else if (et == ElementType::PYRAMID5)
        return "PYRAMID5";
    else if (et == ElementType::PRISM6)
        return "PRISM6";
    else
        throw std::runtime_error(fmt::format("Unsupported element type"));
}

/// Print number in human readable format
///
/// @param value Value to conert into humna-readable string
/// @return Human-readable string
template <typename T>
inline std::string
human_number(T value)
{
    std::string num_str = std::to_string(value);
    std::string result;

    int count = 0;
    for (auto it = num_str.rbegin(); it != num_str.rend(); ++it) {
        if (count && count % 3 == 0) {
            result += ',';
        }
        result += *it;
        ++count;
    }

    std::reverse(result.begin(), result.end());
    return result;
}
