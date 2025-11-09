// SPDX-FileCopyrightText: 2025 (c) David Andrs <andrsd@gmail.com>
// SPDX-License-Identifier: MIT

#include "cxxopts/cxxopts.hpp"
#include <exodusIIcpp/enums.h>
#include <exodusIIcpp/exodusIIcpp.h>
#include <exodusIIcpp/file.h>
#include <fmt/core.h>
#include <set>
#include <stdexcept>
#include <vector>
#include <string>
#include <numeric>
#include <cassert>

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

/// Variable values. Time steps, variables, values
using NodalVariableValues = std::vector<std::vector<std::vector<double>>>;

/// Block ID -> num elements per node
std::map<int, int> num_nodes_per_elem;

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

/// Snap a point to a grid
inline Point
snap_point(const Point & p, double tol)
{
    auto snap = [tol](double v) {
        return std::round(v / tol) * tol;
    };
    return { snap(p.x), snap(p.y), snap(p.z) };
}

/// @param connect Block connectivity (from exodusii) - 1-based indexing
void
remap_connectivity(std::vector<int> & connect, const std::vector<int> & is)
{
    for (auto & idx : connect)
        idx = is[idx - 1] + 1;
}

/// Scatter values from `src` into `dest` using `idx` as a map
void
scatter(const std::vector<double> & src, const std::vector<int> & idx, std::vector<double> & dest)
{
    assert(src.size() == idx.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        dest[idx[i]] = src[i];
    }
}

std::map<int, ElementType>
read_element_types(exodusIIcpp::File & exo)
{
    std::map<int, ElementType> block_element_type;

    for (auto & eb : exo.get_element_blocks()) {
        auto id = eb.get_id();
        auto elem_type_s = eb.get_element_type();
        auto et = element_type(elem_type_s);
        block_element_type.emplace(id, et);
    }

    return block_element_type;
}

void
read_block_ids(exodusIIcpp::File & exo, std::set<int64_t> & block_ids)
{
    for (auto & eb : exo.get_element_blocks()) {
        auto id = eb.get_id();
        block_ids.insert(id);
    }
}

std::vector<int>
read_file(exodusIIcpp::File & exo, int dim, std::map<Point, int> & node_map)
{
    // build nodes
    auto n_nodes = exo.get_num_nodes();
    std::vector<int> is(n_nodes);
    exo.read_coords();
    if (dim == 2) {
        auto x = exo.get_x_coords();
        auto y = exo.get_y_coords();
        for (int i = 0; i < n_nodes; ++i) {
            auto pt = snap_point({ x[i], y[i], 0. }, SNAP_TOLERANCE);
            auto [it, inserted] = node_map.emplace(pt, node_map.size());
            is[i] = it->second;
        }
    }
    else if (dim == 3) {
        auto x = exo.get_x_coords();
        auto y = exo.get_y_coords();
        auto z = exo.get_z_coords();
        for (int i = 0; i < n_nodes; ++i) {
            auto pt = snap_point({ x[i], y[i], z[i] }, SNAP_TOLERANCE);
            auto [it, inserted] = node_map.emplace(pt, node_map.size());
            is[i] = it->second;
        }
    }
    else
        throw std::runtime_error(fmt::format("Unsupported dimension {}", dim));

    return is;
}

std::map<int, std::vector<int>>
read_elements(exodusIIcpp::File & exo)
{
    std::map<int, std::vector<int>> blocks;

    for (auto & eb : exo.get_element_blocks()) {
        auto id = eb.get_id();
        auto elem_type_s = eb.get_element_type();

        auto nn = eb.get_num_nodes_per_element();
        num_nodes_per_elem[id] = nn;

        auto connect = eb.get_connectivity();
        blocks.emplace(id, connect);
    }

    return blocks;
}

NodalVariableValues
read_nodal_vals(exodusIIcpp::File & exo)
{
    auto n_nodal_vars = exo.get_nodal_variable_names().size();

    auto n_times = exo.get_num_times();
    NodalVariableValues nodal_var_values(n_times);
    for (auto & var_vals : nodal_var_values)
        var_vals.resize(n_nodal_vars);

    std::vector<int> nodal_var_indices(n_nodal_vars);
    std::iota(nodal_var_indices.begin(), nodal_var_indices.end(), 0);

    for (int t = 0; t < n_times; ++t)
        for (auto & var_idx : nodal_var_indices)
            nodal_var_values[t][var_idx] = exo.get_nodal_variable_values(t + 1, var_idx + 1);

    return nodal_var_values;
}

void
write_nodes(exodusIIcpp::File & exo, int dim, const std::map<Point, int> & node_map)
{
    auto n_nodes = node_map.size();
    if (dim == 2) {
        std::vector<double> x(n_nodes, 0);
        std::vector<double> y(n_nodes, 0);
        for (auto & [pt, idx] : node_map) {
            x[idx] = pt.x;
            y[idx] = pt.y;
        }
        exo.write_coords(x, y);
    }
    else if (dim == 3) {
        std::vector<double> x(n_nodes, 0);
        std::vector<double> y(n_nodes, 0);
        std::vector<double> z(n_nodes, 0);
        for (auto & [pt, idx] : node_map) {
            x[idx] = pt.x;
            y[idx] = pt.y;
            z[idx] = pt.z;
        }
        exo.write_coords(x, y, z);
    }
    else
        throw std::runtime_error(fmt::format("Unsupported dimension {}", dim));
}

void
write_elements(exodusIIcpp::File & exo,
               const std::set<int64_t> & block_ids,
               const std::map<int, ElementType> & block_element_type,
               const std::map<int, std::vector<int>> & block_connect)
{
    for (auto blk_id : block_ids) {
        int64_t n_elems_in_block = block_connect.at(blk_id).size() / num_nodes_per_elem[blk_id];
        auto elem_type = element_type_str(block_element_type.at(blk_id));
        exo.write_block(blk_id, elem_type, n_elems_in_block, block_connect.at(blk_id));
    }
}

void
write_nodal_variables(exodusIIcpp::File & exo,
                      const std::map<int, std::vector<int>> & index_set,
                      const std::vector<double> & times,
                      std::size_t n_nodes,
                      const std::vector<std::string> & var_names,
                      const std::vector<NodalVariableValues> & var_values)
{
    exo.write_nodal_var_names(var_names);

    std::vector<double> values(n_nodes);
    for (auto t = 0; t < times.size(); ++t) {
        exo.write_time(t + 1, times[t]);

        for (int var_idx = 0; var_idx < var_names.size(); ++var_idx) {
            for (auto fi = 0; fi < var_values.size(); ++fi) {
                const auto & vals = var_values[fi][t][var_idx];
                scatter(vals, index_set.at(fi), values);
            }
            exo.write_nodal_var(t + 1, var_idx + 1, values);
        }

        exo.update();
    }
}

void
join_files(const std::vector<std::string> & inputs, const std::string & output)
{
    // Spatial dimension
    int dim = -1;
    // Mapping node coordinates into global index: Point -> Global ID (0-based)
    std::map<Point, int> node_map;
    /// file index -> global node IDs (0-based)
    std::map<int, std::vector<int>> index_set;
    // Block IDs
    std::set<int64_t> block_ids;
    /// Block ID -> element type
    std::map<int, ElementType> block_element_type;
    // Elements per block: Block ID -> connectivity array (1-based)
    std::map<int, std::vector<int>> block_connect;
    // Nodal var names
    std::vector<std::string> nodal_var_names;
    // Nodal variable values per input file
    std::vector<NodalVariableValues> nodal_vals(inputs.size());
    // Time steps
    std::vector<double> times;

    // read data
    for (int i = 0; i < inputs.size(); ++i) {
        exodusIIcpp::File ex_in(inputs[i], exodusIIcpp::FileAccess::READ);
        ex_in.init();

        dim = ex_in.get_dim();

        ex_in.read_blocks();
        read_block_ids(ex_in, block_ids);
        block_element_type = read_element_types(ex_in);
        index_set[i] = read_file(ex_in, dim, node_map);
        auto blocks = read_elements(ex_in);
        for (auto & [id, connect] : blocks) {
            remap_connectivity(connect, index_set[i]);
            block_connect[id].insert(block_connect[id].end(), connect.begin(), connect.end());
        }

        // TODO: even check var names...
        nodal_var_names = ex_in.get_nodal_variable_names();

        ex_in.read_times();
        // TODO: check that files have the same number of time steps
        times = ex_in.get_times();

        nodal_vals[i] = read_nodal_vals(ex_in);
    }

    // write
    exodusIIcpp::File ex_out(output, exodusIIcpp::FileAccess::WRITE);

    auto n_nodes = node_map.size();
    int64_t n_elems = 0;
    for (auto blk_id : block_ids) {
        int64_t n_elems_in_block = block_connect[blk_id].size() / num_nodes_per_elem[blk_id];
        n_elems += n_elems_in_block;
    }
    int n_elem_blks = block_connect.size();
    int n_node_sets = 0;
    int n_side_sets = 0;
    ex_out.init("", dim, n_nodes, n_elems, n_elem_blks, n_node_sets, n_side_sets);

    write_nodes(ex_out, dim, node_map);
    write_elements(ex_out, block_ids, block_element_type, block_connect);
    write_nodal_variables(ex_out, index_set, times, n_nodes, nodal_var_names, nodal_vals);
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
