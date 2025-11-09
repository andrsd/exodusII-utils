// SPDX-FileCopyrightText: 2025 (c) David Andrs <andrsd@gmail.com>
// SPDX-License-Identifier: MIT

#include "cxxopts/cxxopts.hpp"
#include <exodusIIcpp/enums.h>
#include <exodusIIcpp/exodusIIcpp.h>
#include <fmt/core.h>
#include <set>
#include <stdexcept>
#include <vector>
#include <string>

/// Snap tolerance on points
constexpr double SNAP_TOLERANCE = 1e-10;

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

struct Point {
    double x, y, z;
};

bool
operator<(const Point & a, const Point & b)
{
    if (a.x != b.x)
        return a.x < b.x;
    if (a.y != b.y)
        return a.y < b.y;
    return a.z < b.z;
}

using NodeMap = std::map<int, int>;

/// Global spatial dimension
int dim = -1;
/// Mapping node coordinates into global index: Point -> Global ID
std::map<Point, int> g_node_map;
/// Block IDs
std::set<int64_t> block_ids;
/// Block ID -> element type
std::map<int, ElementType> block_element_type;
/// Block ID -> num elements per node
std::map<int, int> num_nodes_per_elem;
/// Elements per block: Block ID -> connectivity array
std::map<int, std::vector<int>> block_connect;

/// Convert string representation of an element type into enum
ElementType
element_type(std::string_view str)
{
    if (str == "BAR2")
        return ElementType::SEGMENT2;
    else if (str == "TRI" || str == "TRI3")
        return ElementType::TRI3;
    else if (str == "QUAD" || str == "QUAD4")
        return ElementType::QUAD4;
    else
        throw std::runtime_error(fmt::format("Unsupported element type {}", str));
}

const char *
element_type_str(ElementType et)
{
    if (et == ElementType::SEGMENT2)
        return "BAR2";
    else if (et == ElementType::TRI3)
        return "TRI3";
    else if (et == ElementType::QUAD4)
        return "QUAD4";
    else if (et == ElementType::TET4)
        return "TET4";
    else
        throw std::runtime_error(fmt::format("Unsupported element type"));
}

inline Point
snap_point(const Point & p, double tol)
{
    auto snap = [tol](double v) {
        return std::round(v / tol) * tol;
    };
    return { snap(p.x), snap(p.y), snap(p.z) };
}

void
remap_connectivity(std::vector<int> & connect, const NodeMap & node_map)
{
    for (auto & idx : connect) {
        auto it = node_map.find(idx);
        if (it == node_map.end())
            throw std::runtime_error(fmt::format("Failed to find node {} in node map", idx));
        idx = it->second;
    }
}

void
read_file(const std::string & file_name)
{
    exodusIIcpp::File exo(file_name, exodusIIcpp::FileAccess::READ);
    exo.init();

    if (dim == -1)
        dim = exo.get_dim();
    else if (exo.get_dim() != dim)
        throw std::runtime_error(
            fmt::format("Incompatible dimension {} in file {}", exo.get_dim(), file_name));

    // build nodes
    NodeMap node_map;
    exo.read_coords();
    if (dim == 2) {
        auto n_nodes = exo.get_num_nodes();
        auto x = exo.get_x_coords();
        auto y = exo.get_y_coords();
        for (int i = 0; i < n_nodes; ++i) {
            auto pt = snap_point({ x[i], y[i], 0. }, SNAP_TOLERANCE);
            auto [it, inserted] = g_node_map.emplace(pt, g_node_map.size() + 1);
            node_map.emplace(i + 1, it->second);
        }
    }
    else if (dim == 3) {
        auto n_nodes = exo.get_num_nodes();
        auto x = exo.get_x_coords();
        auto y = exo.get_y_coords();
        auto z = exo.get_z_coords();
        for (int i = 0; i < n_nodes; ++i) {
            auto pt = snap_point({ x[i], y[i], z[i] }, SNAP_TOLERANCE);
            auto [it, inserted] = g_node_map.emplace(pt, g_node_map.size() + 1);
            node_map.emplace(i + 1, it->second);
        }
    }
    else
        throw std::runtime_error(fmt::format("Unsupported dimension {}", dim));

    // read elements
    exo.read_blocks();
    for (auto & eb : exo.get_element_blocks()) {
        auto id = eb.get_id();
        auto elem_type_s = eb.get_element_type();
        auto et = element_type(elem_type_s);

        // check that element type matches
        auto it = block_element_type.find(id);
        if (it == block_element_type.end())
            block_element_type.emplace(id, et);
        else if (it->second != et)
            throw std::runtime_error(
                fmt::format("Element type '{}' of block {} does not match across files.",
                            elem_type_s,
                            id));

        block_ids.insert(id);

        auto nn = eb.get_num_nodes_per_element();
        num_nodes_per_elem[id] = nn;

        auto connect = eb.get_connectivity();
        remap_connectivity(connect, node_map);
        block_connect[id].insert(block_connect[id].end(), connect.begin(), connect.end());
    }

    // read variables
}

void
write_file(const std::string & file_name)
{
    exodusIIcpp::File exo(file_name, exodusIIcpp::FileAccess::WRITE);

    auto n_nodes = g_node_map.size();
    int64_t n_elems = 0;
    for (auto blk_id : block_ids) {
        int64_t n_elems_in_block = block_connect[blk_id].size() / num_nodes_per_elem[blk_id];
        n_elems += n_elems_in_block;
    }

    int n_elem_blks = block_connect.size();
    int n_node_sets = 0;
    int n_side_sets = 0;
    exo.init("", dim, n_nodes, n_elems, n_elem_blks, n_node_sets, n_side_sets);

    // write nodes
    if (dim == 2) {
        std::vector<double> x(n_nodes, 0);
        std::vector<double> y(n_nodes, 0);
        for (auto & [pt, idx] : g_node_map) {
            auto i = idx - 1;
            x[i] = pt.x;
            y[i] = pt.y;
        }
        exo.write_coords(x, y);
    }
    else if (dim == 3) {
        std::vector<double> x(n_nodes, 0);
        std::vector<double> y(n_nodes, 0);
        std::vector<double> z(n_nodes, 0);
        for (auto & [pt, idx] : g_node_map) {
            auto i = idx - 1;
            x[i] = pt.x;
            y[i] = pt.y;
            z[i] = pt.z;
        }
        exo.write_coords(x, y, z);
    }
    else
        throw std::runtime_error(fmt::format("Unsupported dimension {}", dim));

    // write elements
    for (auto blk_id : block_ids) {
        int64_t n_elems_in_block = block_connect[blk_id].size() / num_nodes_per_elem[blk_id];
        auto elem_type = element_type_str(block_element_type[blk_id]);
        exo.write_block(blk_id, elem_type, n_elems_in_block, block_connect[blk_id]);
    }

    // write variables
}

void
join_files(const std::vector<std::string> & inputs, const std::string & output)
{
    for (auto & s : inputs)
        read_file(s);
    write_file(output);
}

int
main(int argc, char * argv[])
{
    cxxopts::Options options("exo-join", "Join multiple exodusII files into one");

    // clang-format off
    options.add_options()
        ("help", "Show this help page")
        ("v,version", "Show the version")
        ("files", "files", cxxopts::value<std::vector<std::string>>())
    ;
    options.parse_positional({ "files" });
    options.positional_help("<inputs> <output>");
    // clang-format on

    cxxopts::ParseResult result;
    try {
        result = options.parse(argc, argv);

        if (result.count("version"))
            fmt::println(stdout, "exo-join version 0.0.0");

        else if (result.count("files") > 2) {
            auto inputs = result["files"].as<std::vector<std::string>>();
            auto output = inputs.back();
            inputs.pop_back();
            join_files(inputs, output);
        }

        else
            fmt::print(stdout, "{}", options.help());

        return 0;
    }
    catch (const cxxopts::exceptions::exception & e) {
        fmt::print(stderr, "Error: {}\n", e.what());
        fmt::print(stdout, "{}", options.help());
        return 1;
    }
    catch (std::exception & e) {
        fmt::print(stderr, "Error: {}\n", e.what());
        return 1;
    }
}
