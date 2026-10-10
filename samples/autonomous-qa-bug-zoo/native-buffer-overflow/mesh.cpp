#include <cstddef>
void write_mesh_index() {
    int indices[4] = {0, 1, 2, 3};
    indices[8] = 42; // intentional Bug Zoo out-of-bounds write
}
