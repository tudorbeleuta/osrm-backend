#ifndef GEOMETRY_COMPRESSOR_HPP_
#define GEOMETRY_COMPRESSOR_HPP_

#include "util/typedefs.hpp"

// DELETE
#include "extractor/query_node.hpp"


#include <unordered_map>
#include <string>
#include <vector>

namespace osrm
{
namespace extractor
{

class CompressedEdgeContainer
{
  public:
    struct CompressedEdge
    {
      public:
        NodeID node_id;    // refers to an internal node-based-node
        EdgeWeight forward_weight;
        EdgeWeight reverse_weight;
    };
    using EdgeBucket = std::vector<CompressedEdge>;

    CompressedEdgeContainer();

    void CompressEdge(const EdgeID f_edge_id_1,
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
                      const std::vector<QueryNode> &node_info_list);

    void
    AddUncompressedEdge(const EdgeID edgei_id, const NodeID target_node, const EdgeWeight weight);

    bool HasEntryForID(const EdgeID edge_id) const;
    void PrintStatistics() const;
    void SerializeInternalVector(const std::string &path) const;
    unsigned GetPositionForID(const EdgeID edge_id) const;
    const EdgeBucket &GetBucketReference(const EdgeID edge_id) const;
    bool IsTrivial(const EdgeID edge_id) const;
    NodeID GetFirstEdgeTargetID(const EdgeID edge_id) const;
    NodeID GetLastEdgeTargetID(const EdgeID edge_id) const;
    NodeID GetLastEdgeSourceID(const EdgeID edge_id) const;

  private:
    int free_list_maximum = 0;

    void IncreaseFreeList();
    std::vector<EdgeBucket> m_compressed_geometries;
    std::vector<unsigned> m_free_list;
    std::unordered_map<EdgeID, unsigned> m_edge_id_to_list_index_map;
};
}
}

#endif // GEOMETRY_COMPRESSOR_HPP_
