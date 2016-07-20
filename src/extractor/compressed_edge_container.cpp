#include "extractor/compressed_edge_container.hpp"
#include "util/simple_logger.hpp"

#include <boost/assert.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <limits>
#include <string>
#include <numeric>

#include <iostream>

namespace osrm
{
namespace extractor
{

CompressedEdgeContainer::CompressedEdgeContainer()
{
    m_free_list.reserve(100);
    IncreaseFreeList();
}

void CompressedEdgeContainer::IncreaseFreeList()
{
    m_compressed_geometries.resize(m_compressed_geometries.size() + 100);
    for (unsigned i = 100; i > 0; --i)
    {
        m_free_list.emplace_back(free_list_maximum);
        ++free_list_maximum;
    }
}

bool CompressedEdgeContainer::HasEntryForID(const EdgeID edge_id) const
{
    auto iter = m_edge_id_to_list_index_map.find(edge_id);
    return iter != m_edge_id_to_list_index_map.end();
}

unsigned CompressedEdgeContainer::GetPositionForID(const EdgeID edge_id) const
{
    auto map_iterator = m_edge_id_to_list_index_map.find(edge_id);
    BOOST_ASSERT(map_iterator != m_edge_id_to_list_index_map.end());
    BOOST_ASSERT(map_iterator->second < m_compressed_geometries.size());
    return map_iterator->second;
}

void CompressedEdgeContainer::SerializeInternalVector(const std::string &path) const
{

    boost::filesystem::fstream geometry_out_stream(path, std::ios::binary | std::ios::out);
    const unsigned compressed_geometries = m_compressed_geometries.size() + 1;
    BOOST_ASSERT(std::numeric_limits<unsigned>::max() != compressed_geometries);
    geometry_out_stream.write((char *)&compressed_geometries, sizeof(unsigned));

    // write indices array
    unsigned prefix_sum_of_list_indices = 0;
    for (const auto &elem : m_compressed_geometries)
    {
        geometry_out_stream.write((char *)&prefix_sum_of_list_indices, sizeof(unsigned));

        const std::vector<CompressedEdge> &current_vector = elem;
        const unsigned unpacked_size = current_vector.size();
        BOOST_ASSERT(std::numeric_limits<unsigned>::max() != unpacked_size);
        prefix_sum_of_list_indices += unpacked_size;
    }
    // sentinel element
    geometry_out_stream.write((char *)&prefix_sum_of_list_indices, sizeof(unsigned));

    // number of geometry entries to follow, it is the (inclusive) prefix sum
    geometry_out_stream.write((char *)&prefix_sum_of_list_indices, sizeof(unsigned));

    unsigned control_sum = 0;
    // write compressed geometries
    for (auto &elem : m_compressed_geometries)
    {
        const std::vector<CompressedEdge> &current_vector = elem;
        const unsigned unpacked_size = current_vector.size();
        control_sum += unpacked_size;
        BOOST_ASSERT(std::numeric_limits<unsigned>::max() != unpacked_size);
        for (const auto &current_node : current_vector)
        {
            geometry_out_stream.write((char *)&(current_node), sizeof(CompressedEdge));
        }
    }
    BOOST_ASSERT(control_sum == prefix_sum_of_list_indices);
}

// Adds info for a compressed edge to the container.   edge_id_2
// has been removed from the graph, so we have to save These edges/nodes
// have already been trimmed from the graph, this function just stores
// the original data for unpacking later.
//
//     edge_id_1               edge_id_2
//   ----------> via_node_id -----------> target_node_id
//     weight_1                weight_2
// TODO TODO Update comment
void CompressedEdgeContainer::CompressEdge(const EdgeID f_edge_id_1,
                                           const EdgeID f_edge_id_2,
                                           const EdgeID r_edge_id_1,
                                           const EdgeID r_edge_id_2,
                                           const NodeID node_u,
                                           const NodeID node_v,
                                           const NodeID node_w,
                                           const EdgeWeight f_weight1,
                                           const EdgeWeight f_weight2,
                                           const EdgeWeight r_weight1,
                                           const EdgeWeight r_weight2,

                                           // DELETE
                                           const std::vector<QueryNode> &node_info_list)
{
    util::SimpleLogger().Write() << "uvw: ";
    util::SimpleLogger().Write() << "     " << node_u << " " << node_info_list[node_u].node_id;
    util::SimpleLogger().Write() << "     " << node_v << " " << node_info_list[node_v].node_id;
    util::SimpleLogger().Write() << "     " << node_w << " " << node_info_list[node_w].node_id;

    // remove super-trivial geometries
    BOOST_ASSERT(SPECIAL_EDGEID != f_edge_id_1);
    BOOST_ASSERT(SPECIAL_EDGEID != f_edge_id_2);
    BOOST_ASSERT(SPECIAL_EDGEID != r_edge_id_1);
    BOOST_ASSERT(SPECIAL_EDGEID != r_edge_id_2);
    BOOST_ASSERT(SPECIAL_NODEID != node_u);
    BOOST_ASSERT(SPECIAL_NODEID != node_v);
    BOOST_ASSERT(SPECIAL_NODEID != node_w);
    BOOST_ASSERT(INVALID_EDGE_WEIGHT != f_weight1);
    BOOST_ASSERT(INVALID_EDGE_WEIGHT != f_weight2);
    BOOST_ASSERT(INVALID_EDGE_WEIGHT != r_weight1);
    BOOST_ASSERT(INVALID_EDGE_WEIGHT != r_weight2);

    bool left_exists = HasEntryForID(f_edge_id_1);
    bool right_exists = HasEntryForID(f_edge_id_2);

    BOOST_ASSERT(left_exists == HasEntryForID(r_edge_id_2));
    BOOST_ASSERT(right_exists == HasEntryForID(r_edge_id_1));

    unsigned edge_bucket_index;

    if (!left_exists)
    {
        // create a new bucket
        if (0 == m_free_list.size())
        {
            // make sure there is a place to put the entries
            IncreaseFreeList();
        }
        BOOST_ASSERT(!m_free_list.empty());
        edge_bucket_index = m_free_list.back();
        m_edge_id_to_list_index_map[f_edge_id_1] = edge_bucket_index;
        m_free_list.pop_back();

        BOOST_ASSERT(edge_bucket_index == GetPositionForID(f_edge_id_1));
    }
    else
    {
        edge_bucket_index = GetPositionForID(f_edge_id_1);

        // since we'll be appending to the end of this list, r_edge_id_2 will now be the middle
        // of an edge, not necessary to track in the index map
        BOOST_ASSERT(m_edge_id_to_list_index_map[r_edge_id_2] == edge_bucket_index);
        m_edge_id_to_list_index_map.erase(r_edge_id_2);
    }

    BOOST_ASSERT(edge_bucket_index < m_compressed_geometries.size());

    std::vector<CompressedEdge> &edge_bucket_list = m_compressed_geometries[edge_bucket_index];

    if (edge_bucket_list.empty())
    {
        // Add both the left and middle nodes
        edge_bucket_list.emplace_back(CompressedEdge{node_u, INVALID_EDGE_WEIGHT, r_weight2});
        edge_bucket_list.emplace_back(CompressedEdge{node_v, f_weight1, r_weight1});
    }
    else
    {
        // Both left and middle nodes should already exist, but the middle node, which
        // was previously an end node, now gets a reverse weight
        BOOST_ASSERT(edge_bucket_list.front().node_id == node_u);
        BOOST_ASSERT(edge_bucket_list.back().node_id == node_v);
        BOOST_ASSERT(edge_bucket_list.front().forward_weight == INVALID_EDGE_WEIGHT);
        BOOST_ASSERT(edge_bucket_list.back().reverse_weight == INVALID_EDGE_WEIGHT);

        // This may or may not be the correct weight, depending on whether the second
        // edge is atomic, but we address that below
        edge_bucket_list.back().reverse_weight = r_weight1;
    }

    BOOST_ASSERT(1 < edge_bucket_list.size());
    BOOST_ASSERT(!edge_bucket_list.empty());

    if (right_exists)
    {
        // second edge is not atomic anymore
        const unsigned list_to_remove_index = GetPositionForID(f_edge_id_2);
        BOOST_ASSERT(list_to_remove_index == GetPositionForID(r_edge_id_1));
        BOOST_ASSERT(list_to_remove_index < m_compressed_geometries.size());

        std::vector<CompressedEdge> &edge_bucket_list_to_remove =
            m_compressed_geometries[list_to_remove_index];

        // util::SimpleLogger().Write() << "nodes we think match: " << edge_bucket_list.back().node_id << " " <<
        //     edge_bucket_list_to_remove.front().node_id << " " <<
        //     node_info_list[edge_bucket_list.back().node_id].node_id << " " <<
        //     node_info_list[edge_bucket_list_to_remove.front().node_id].node_id;

        // util::SimpleLogger().Write() << "first vec nodes: ";
        // for (const auto &n : edge_bucket_list)
        // {
        //     util::SimpleLogger().Write() << "    " << n.node_id << " " << node_info_list[n.node_id].node_id;
        // }
        // util::SimpleLogger().Write() << "second vec nodes: ";
        // for (const auto &n : edge_bucket_list_to_remove)
        // {
        //     util::SimpleLogger().Write() << "    " << n.node_id << " " << node_info_list[n.node_id].node_id;
        // }

        BOOST_ASSERT(edge_bucket_list.back().node_id == edge_bucket_list_to_remove.front().node_id);
        BOOST_ASSERT(edge_bucket_list_to_remove.front().forward_weight == INVALID_EDGE_WEIGHT);
        BOOST_ASSERT(r_weight1 == std::accumulate(edge_bucket_list_to_remove.begin(),
                                                  edge_bucket_list_to_remove.end() - 1, 0,
                                                  [](const EdgeWeight& a, CompressedEdge b) {
                                                     return a + b.reverse_weight;
                                                  }));

        // Here is where we address the incorrect reverse weight mentioned above
        edge_bucket_list.back().reverse_weight = edge_bucket_list_to_remove.front().reverse_weight;

        // move this existing list to the end of the first. Note since we now store beginning
        // nodes, we skip the first node in the second list as it is a dupe
        edge_bucket_list.insert(edge_bucket_list.end(), edge_bucket_list_to_remove.begin() + 1,
            edge_bucket_list_to_remove.end());

        // remove the list of f_edge_id_2/r_edge_id_1 and update the index map
        m_edge_id_to_list_index_map.erase(f_edge_id_2);
        BOOST_ASSERT(m_edge_id_to_list_index_map.end() ==
                     m_edge_id_to_list_index_map.find(f_edge_id_2));
        m_edge_id_to_list_index_map[r_edge_id_1] = edge_bucket_index;
        BOOST_ASSERT(GetPositionForID(r_edge_id_1) == GetPositionForID(f_edge_id_1));
        edge_bucket_list_to_remove.clear();
        BOOST_ASSERT(0 == edge_bucket_list_to_remove.size());
        m_free_list.emplace_back(list_to_remove_index);
        BOOST_ASSERT(list_to_remove_index == m_free_list.back());
    }
    else
    {
        // we are certain that the second edge is atomic
        edge_bucket_list.emplace_back(CompressedEdge{node_w, f_weight2, INVALID_EDGE_WEIGHT});
        m_edge_id_to_list_index_map[r_edge_id_1] = edge_bucket_index;
    }
}

void CompressedEdgeContainer::AddUncompressedEdge(const EdgeID edge_id,
                                                  const NodeID target_node_id,
                                                  const EdgeWeight weight)
{
    // remove super-trivial geometries
    BOOST_ASSERT(SPECIAL_EDGEID != edge_id);
    BOOST_ASSERT(SPECIAL_NODEID != target_node_id);
    BOOST_ASSERT(INVALID_EDGE_WEIGHT != weight);

    // Add via node id. List is created if it does not exist
    if (!HasEntryForID(edge_id))
    {
        // create a new entry in the map
        if (0 == m_free_list.size())
        {
            // make sure there is a place to put the entries
            IncreaseFreeList();
        }
        BOOST_ASSERT(!m_free_list.empty());
        m_edge_id_to_list_index_map[edge_id] = m_free_list.back();
        m_free_list.pop_back();
    }

    // find bucket index
    const auto iter = m_edge_id_to_list_index_map.find(edge_id);
    BOOST_ASSERT(iter != m_edge_id_to_list_index_map.end());
    const unsigned edge_bucket_id = iter->second;
    BOOST_ASSERT(edge_bucket_id == GetPositionForID(edge_id));
    BOOST_ASSERT(edge_bucket_id < m_compressed_geometries.size());

    std::vector<CompressedEdge> &edge_bucket_list = m_compressed_geometries[edge_bucket_id];

    // TODO HERE
    // note we don't save the start coordinate: it is implicitly given by edge_id
    // weight is the distance to the (currently) last coordinate in the bucket
    // Don't re-add this if it's already in there.
    if (edge_bucket_list.empty())
    {
        // TODO HERE
        edge_bucket_list.emplace_back(CompressedEdge{target_node_id, weight});
    }
}

void CompressedEdgeContainer::PrintStatistics() const
{
    const uint64_t compressed_edges = m_compressed_geometries.size();
    BOOST_ASSERT(0 == compressed_edges % 2);
    BOOST_ASSERT(m_compressed_geometries.size() + m_free_list.size() > 0);

    uint64_t compressed_geometries = 0;
    uint64_t longest_chain_length = 0;
    for (const std::vector<CompressedEdge> &current_vector : m_compressed_geometries)
    {
        compressed_geometries += current_vector.size();
        longest_chain_length = std::max(longest_chain_length, (uint64_t)current_vector.size());
    }

    util::SimpleLogger().Write()
        << "Geometry successfully removed:"
           "\n  compressed edges: "
        << compressed_edges << "\n  compressed geometries: " << compressed_geometries
        << "\n  longest chain length: " << longest_chain_length << "\n  cmpr ratio: "
        << ((float)compressed_edges / std::max(compressed_geometries, (uint64_t)1))
        << "\n  avg chain length: "
        << (float)compressed_geometries / std::max((uint64_t)1, compressed_edges);
}

const CompressedEdgeContainer::EdgeBucket &
CompressedEdgeContainer::GetBucketReference(const EdgeID edge_id) const
{
    const unsigned index = m_edge_id_to_list_index_map.at(edge_id);
    util::SimpleLogger().Write() << "eid/bucket index: " << edge_id << " " << index;
    return m_compressed_geometries.at(index);
}

// Since all edges are technically in the compressed geometry container,
// regardless of whether a compressed edge actually contains multiple
// original segments, we use 'Trivial' here to describe compressed edges
// that only contain one original segment
bool CompressedEdgeContainer::IsTrivial(const EdgeID edge_id) const
{
    const auto &bucket = GetBucketReference(edge_id);
    return bucket.size() == 1;
}

NodeID CompressedEdgeContainer::GetFirstEdgeTargetID(const EdgeID edge_id) const
{
    const auto &bucket = GetBucketReference(edge_id);
    BOOST_ASSERT(bucket.size() >= 1);
    return bucket.front().node_id;
}
NodeID CompressedEdgeContainer::GetLastEdgeTargetID(const EdgeID edge_id) const
{
    const auto &bucket = GetBucketReference(edge_id);
    BOOST_ASSERT(bucket.size() >= 1);
    return bucket.back().node_id;
}
NodeID CompressedEdgeContainer::GetLastEdgeSourceID(const EdgeID edge_id) const
{
    const auto &bucket = GetBucketReference(edge_id);
    BOOST_ASSERT(bucket.size() >= 2);
    return bucket[bucket.size() - 2].node_id;
}
}
}
