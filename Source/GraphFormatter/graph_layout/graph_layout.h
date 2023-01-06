/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Howaajin. All rights reserved.
 *  Licensed under the MIT License. See License in the project root for license information.
 *--------------------------------------------------------------------------------------------*/

#pragma once

#include <utility>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <functional>
#include <iostream>

namespace graph_layout
{
    struct graph_t;
    struct node_t;
    struct vector2_t;

    enum class pin_type_t
    {
        in,
        out,
    };

    struct vector2_t
    {
        float x, y;

        vector2_t operator+(const vector2_t& other) const
        {
            return vector2_t{this->x + other.x, this->y + other.y};
        }

        vector2_t operator-(const vector2_t& other) const
        {
            return vector2_t{this->x - other.x, this->y - other.y};
        }
    };

    struct rect_t
    {
        float l, t, r, b;

        void offset_by(vector2_t offset)
        {
            l += offset.x;
            r += offset.x;
            t += offset.y;
            b += offset.y;
        }
    };

    struct pin_t
    {
        pin_type_t type = pin_type_t::in;
        vector2_t offset{0, 0};
        node_t* owner = nullptr;
    };

    struct edge_t
    {
        pin_t* tail = nullptr;
        pin_t* head = nullptr;
        int weight = 1;
        int min_length = 1;
        int cut_value = 0;
        bool is_inverted = false;
        int length() const;
        int slack() const;
    };

    struct node_t
    {
        std::string name;
        int rank{-1};
        // Is the node belongs to the head component?
        bool belongs_to_head = false;
        // Is the node belongs to the tail component?
        bool belongs_to_tail = false;
        graph_t* graph = nullptr;
        vector2_t position{0, 0};
        std::vector<edge_t*> in_edges;
        std::vector<edge_t*> out_edges;
        std::vector<pin_t*> pins;
        bool is_descendant_of(node_t* node) const;
        void set_position(vector2_t p);
        pin_t* add_pin(pin_type_t type);
        std::set<node_t*> get_direct_connected_nodes(std::function<bool(edge_t*)> filter) const;
        ~node_t();
        node_t* clone() const;
    };

    struct tree_t
    {
        std::set<edge_t*> tree_edges;
        std::set<edge_t*> non_tree_edges;
        std::set<node_t*> nodes;
        edge_t* find_min_incident_edge(node_t** incident_node);
        void tighten() const;
        tree_t tight_sub_tree() const;
        edge_t* leave_edge() const;
        edge_t* enter_edge(edge_t* edge);
        void exchange(edge_t* e, edge_t* f);
        void calculate_cut_values();
        void update_non_tree_edges(const std::set<edge_t*>& all_edges);
    private:
        void reset_head_or_tail() const;
        void split_to_head_tail(edge_t* edge);
        void mark_head_or_tail(node_t* n, edge_t* cut_edge, bool is_head);
        static void add_to_weights(const edge_t* edge, int& head_to_tail_weight, int& tail_to_head_weight);
    };

    enum class rank_slot_t { none, min, max, };

    struct graph_t
    {
        rect_t bound{0, 0, 0, 0};
        std::vector<node_t*> nodes;
        node_t* min_ranking_node = nullptr;
        node_t* max_ranking_node = nullptr;
        std::map<std::pair<pin_t*, pin_t*>, edge_t*> edges;
        std::vector<std::vector<node_t*>> layers;
        std::map<node_t*, graph_t*> sub_graphs;

        graph_t* clone() const;
        graph_t* clone(std::map<node_t*, node_t*>& nodes_map, std::map<pin_t*, pin_t*>& pins_map, std::map<edge_t*, edge_t*>& edges_map,
                       std::map<node_t*, node_t*>& nodes_map_inv, std::map<pin_t*, pin_t*>& pins_map_inv, std::map<edge_t*, edge_t*>& edges_map_inv) const;
        ~graph_t();

        node_t* add_node(graph_t* sub_graph = nullptr);
        node_t* add_node(const std::string& name, graph_t* sub_graph = nullptr);
        void set_node_in_rank_slot(node_t* node, rank_slot_t rank_slot);
        void remove_node(node_t* node);

        edge_t* add_edge(pin_t* tail, pin_t* head);
        void remove_edge(const edge_t* edge);
        void remove_edge(pin_t* tail, pin_t* head);
        void invert_edge(edge_t* edge) const;
        void merge_edges();

        std::vector<pin_t*> get_pins() const;
        std::vector<node_t*> get_source_nodes() const;
        std::vector<node_t*> get_sink_nodes() const;

        void translate(vector2_t offset);
        void set_position(vector2_t position);
        void acyclic() const;
        void rank();
        tree_t feasible_tree();
        std::string generate_test_code();
        static void test();
    private:
        void init_rank();
        void normalize();
        tree_t tight_tree() const;
        std::vector<node_t*> get_nodes_without_unscanned_in_edges(const std::set<node_t*>& visited, const std::set<edge_t*>& scanned_set) const;
    };
}