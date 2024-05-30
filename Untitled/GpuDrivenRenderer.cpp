#include "GpuDrivenRenderer.hpp"
#include "meshoptimizer/meshoptimizer.h"

/*
1. first on cpu
prepare the meshlets and their bounds and send them to gpu
2. do gpu culling

resources (create/destroy)

mesh/meshlet resources:
StructuredBuffers:
   meshlet_data_sb 
   meshlets_vertex_data_sb
   meshlets_vertex_pos_sb
   meshlets_sb
   meshes_sb
   mesh_bounds_sb
   mesh_instances_sb

per frame resources
assuming k_max_frames = 2

   meshlets_instances_sb[ k_max_frames ];
   meshlets_index_buffer_sb[k_max_frames];
   meshlets_visible_instances_sb[k_max_frames];


geometry (gbuffer) pass prepare
Indirect draw commands
   mesh_task_indirect_count_early_sb[k_max_frames];
   mesh_task_indirect_early_commands_sb[k_max_frames];
   mesh_task_indirect_culled_commands_sb[k_max_frames];

   mesh_task_indirect_count_late_sb[k_max_frames];
   mesh_task_indirect_late_commands_sb[k_max_frames];

   meshlet_instances_indirect_count_sb[k_max_frames];


- Indirect dispatch calls

- Generate meshlet list

- Cull visible meshlets

- Write counts

- Generate index buffer

main gbuffer renderpass
- draw_mesh_task_indirect_count


gltf-scene

Mesh optimizer:
add/link the library

when importing gltf, implement "add_mesh" using library "meshopt_buildMeshlets"

fill the meshlet triangles, indices, vertices arrays

compute meshlet bounds using meshopt_computeMeshletBounds

store geometry_transform

*/


