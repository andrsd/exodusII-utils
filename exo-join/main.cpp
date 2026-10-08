// SPDX-FileCopyrightText: 2025 (c) David Andrs <andrsd@gmail.com>
// SPDX-License-Identifier: MIT

#include <cstdlib>
#include "common.h"
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

/// Elemental variable values. Time steps, {(variable ID, block ID) -> values}
using ElementalVariableValues = std::vector<std::map<std::pair<int, int>, std::vector<double>>>;

/// Block ID -> num elements per node
std::map<int, int> num_nodes_per_elem;

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

/// Shift `data` by `ofst`
void
shift(std::vector<int> & data, int ofst)
{
    for (auto & v : data)
        v += ofst;
}

void
read_element_types(exodusIIcpp::File & exo, std::map<int, ElementType> & block_element_type)
{
    for (auto & eb : exo.get_element_blocks()) {
        auto id = eb.get_id();
        auto elem_type_s = eb.get_element_type();
        auto et = element_type(elem_type_s);
        block_element_type.try_emplace(id, et);
    }
}

void
read_block_ids(exodusIIcpp::File & exo, std::set<int64_t> & block_ids)
{
    for (auto & eb : exo.get_element_blocks()) {
        auto id = eb.get_id();
        block_ids.insert(id);
    }
}

std::vector<Point>
read_coordinates(exodusIIcpp::File & exo, int dim)
{
    // build nodes
    auto n_nodes = exo.get_num_nodes();
    std::vector<Point> nodes;
    nodes.reserve(n_nodes);
    exo.read_coords();
    if (dim == 2) {
        auto x = exo.get_x_coords();
        auto y = exo.get_y_coords();
        for (int i = 0; i < n_nodes; ++i) {
            Point pt(x[i], y[i], 0.);
            nodes.emplace_back(pt);
        }
    }
    else if (dim == 3) {
        auto x = exo.get_x_coords();
        auto y = exo.get_y_coords();
        auto z = exo.get_z_coords();
        for (int i = 0; i < n_nodes; ++i) {
            Point pt(x[i], y[i], z[i]);
            nodes.emplace_back(pt);
        }
    }
    else
        throw std::runtime_error(fmt::format("Unsupported dimension {}", dim));
    return nodes;
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

ElementalVariableValues
read_elemental_vals(exodusIIcpp::File & exo)
{
    auto n_elem_vars = exo.get_elemental_variable_names().size();

    auto n_times = exo.get_num_times();
    ElementalVariableValues elem_var_values(n_times);

    auto tt = exo.get_elemental_var_table();

    auto & el_blks = exo.get_element_blocks();

    std::vector<int> elem_var_indices(n_elem_vars);
    std::iota(elem_var_indices.begin(), elem_var_indices.end(), 0);

    for (int t = 0; t < n_times; ++t) {
        for (auto & var_idx : elem_var_indices) {
            for (std::size_t blk_idx = 0; blk_idx < el_blks.size(); blk_idx++) {
                if (tt(blk_idx + 1, var_idx + 1)) {
                    auto blk_id = el_blks[blk_idx].get_id();
                    auto vals = exo.get_elemental_variable_values(t + 1, var_idx + 1, blk_id);
                    elem_var_values[t][{ var_idx, blk_id }] = vals;
                }
            }
        }
    }

    return elem_var_values;
}

std::vector<std::vector<double>>
read_global_vals(const exodusIIcpp::File & exo)
{
    std::vector<std::vector<double>> vals;
    auto n_times = exo.get_num_times();
    vals.reserve(n_times);
    for (int t = 0; t < n_times; t++) {
        auto step_vals = exo.get_global_variable_values(t + 1);
        vals.emplace_back(std::move(step_vals));
    }
    return vals;
}

void
write_nodes(exodusIIcpp::File & exo, int dim, const std::vector<std::vector<Point>> & node_map)
{
    auto n_nodes = 0;
    for (auto & pts : node_map)
        n_nodes += pts.size();

    std::vector<double> x;
    std::vector<double> y;
    std::vector<double> z;

    if (dim == 2) {
        x.reserve(n_nodes);
        y.reserve(n_nodes);
        for (auto & f_pts : node_map) {
            for (auto & pt : f_pts) {
                x.emplace_back(pt.x);
                y.emplace_back(pt.y);
            }
        }
        exo.write_coords(x, y);
    }
    else if (dim == 3) {
        x.reserve(n_nodes);
        y.reserve(n_nodes);
        z.reserve(n_nodes);
        for (auto & f_pts : node_map) {
            for (auto & pt : f_pts) {
                x.emplace_back(pt.x);
                y.emplace_back(pt.y);
                z.emplace_back(pt.z);
            }
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
write_block_names(exodusIIcpp::File & exo,
                  const std::set<int64_t> & block_ids,
                  const std::map<int64_t, std::string> & block_names)
{
    std::vector<std::string> names;
    for (auto id : block_ids)
        names.push_back(block_names.at(id));

    if (not names.empty())
        exo.write_block_names(names);
}

/// Build elemental variable by joining values from all files
std::vector<double>
join_elemental_variable(std::size_t t,
                        std::size_t var_idx,
                        std::size_t block_id,
                        const std::vector<ElementalVariableValues> & elem_vals)
{
    std::size_t n_elems = 0;
    for (std::size_t fi = 0; fi < elem_vals.size(); ++fi) {
        n_elems += elem_vals[fi][t].at({ var_idx, block_id }).size();
    }

    std::vector<double> values;
    values.reserve(n_elems);
    for (std::size_t fi = 0; fi < elem_vals.size(); ++fi) {
        const auto & vals = elem_vals[fi][t].at({ var_idx, block_id });
        values.insert(values.end(), vals.begin(), vals.end());
    }

    return values;
}

void
write_elemental_variable(exodusIIcpp::File & exo,
                         int step_num,
                         int var_index,
                         int64_t block_id,
                         const std::vector<double> & values)
{
    for (std::size_t i = 0; i < values.size(); ++i) {
        exo.write_partial_elem_var(step_num + 1, var_index + 1, block_id, i + 1, values[i]);
    }
}

void
join_files(const std::vector<std::string> & inputs, const std::string & output)
{
    // Spatial dimension
    int dim = -1;
    // Node coordinates: file -> `Point`s
    std::vector<std::vector<Point>> node_map(inputs.size());
    // Block IDs
    std::set<int64_t> block_ids;
    /// Block ID -> element type
    std::map<int, ElementType> block_element_type;
    // Elements per block: Block ID -> connectivity array (1-based)
    std::map<int, std::vector<int>> block_connect;
    // Element block names
    std::map<int64_t, std::string> block_names;
    // Nodal var names
    std::vector<std::string> nodal_var_names;
    // Nodal variable values per input file
    std::vector<NodalVariableValues> nodal_vals(inputs.size());
    // Elemental var names
    std::vector<std::string> elem_var_names;
    // Elemental variable values per input file
    std::vector<ElementalVariableValues> elem_vals(inputs.size());
    // Time steps
    std::vector<double> times;
    // Global variable names
    std::vector<std::string> global_var_names;
    // Global variable values
    std::vector<std::vector<double>> global_vals;

    int connect_ofst = 0;
    // read data
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        exodusIIcpp::File ex_in(inputs[i], exodusIIcpp::FileAccess::READ);
        ex_in.init();

        dim = ex_in.get_dim();

        ex_in.read_blocks();
        read_block_ids(ex_in, block_ids);
        read_element_types(ex_in, block_element_type);
        node_map[i] = read_coordinates(ex_in, dim);
        auto blocks = read_elements(ex_in);
        for (auto & [id, connect] : blocks) {
            shift(connect, connect_ofst);
            block_connect[id].insert(block_connect[id].end(), connect.begin(), connect.end());
        }
        connect_ofst += node_map[i].size();

        // block names
        for (auto [id, name] : ex_in.read_block_names())
            block_names[id] = name;

        // TODO: even check var names...
        nodal_var_names = ex_in.get_nodal_variable_names();

        // TODO: check variable name. now, we assume that all files have the same
        // elemental variable names
        elem_var_names = ex_in.get_elemental_variable_names();

        ex_in.read_times();
        // TODO: check that files have the same number of time steps
        times = ex_in.get_times();

        nodal_vals[i] = read_nodal_vals(ex_in);
        elem_vals[i] = read_elemental_vals(ex_in);

        // TODO: check var names, possibly even merge
        if (i == 0) {
            // only grab values from the first part file, for right now
            global_var_names = ex_in.get_global_variable_names();
            if (not global_var_names.empty())
                global_vals = read_global_vals(ex_in);
        }
    }

    // write
    exodusIIcpp::File ex_out(output, exodusIIcpp::FileAccess::WRITE);

    auto n_nodes = 0;
    for (auto & a : node_map)
        n_nodes += a.size();
    int64_t n_elems = 0;
    for (auto blk_id : block_ids)
        n_elems += block_connect[blk_id].size() / num_nodes_per_elem[blk_id];
    int n_elem_blks = block_connect.size();
    int n_node_sets = 0;
    int n_side_sets = 0;
    ex_out.init("", dim, n_nodes, n_elems, n_elem_blks, n_node_sets, n_side_sets);

    write_nodes(ex_out, dim, node_map);
    write_elements(ex_out, block_ids, block_element_type, block_connect);
    write_block_names(ex_out, block_ids, block_names);

    ex_out.write_nodal_var_names(nodal_var_names);
    ex_out.write_elem_var_names(elem_var_names);
    if (not global_var_names.empty())
        ex_out.write_global_var_names(global_var_names);
    for (std::size_t t = 0; t < times.size(); ++t) {
        ex_out.write_time(t + 1, times[t]);

        {
            std::vector<double> values(n_nodes);
            for (std::size_t var_idx = 0; var_idx < nodal_var_names.size(); ++var_idx) {
                std::size_t iidx = 0;
                for (std::size_t fi = 0; fi < nodal_vals.size(); ++fi) {
                    const auto & vals = nodal_vals[fi][t][var_idx];
                    for (std::size_t i = 0; i < vals.size(); ++i) {
                        values[iidx++] = vals[i];
                    }
                }
                ex_out.write_nodal_var(t + 1, var_idx + 1, values);
            }
        }
        // elemental variables
        // NOTE: we grap 0th file, since we assume the same map in each file
        for (auto & [k, _] : elem_vals[0][t]) {
            auto [var_idx, block_id] = k;
            auto values = join_elemental_variable(t, var_idx, block_id, elem_vals);
            write_elemental_variable(ex_out, t, var_idx, block_id, values);
        }

        {
            for (std::size_t var_idx = 0; var_idx < global_var_names.size(); ++var_idx)
                ex_out.write_global_var(t + 1, var_idx + 1, global_vals[t][var_idx]);
        }

        ex_out.update();
    }
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
