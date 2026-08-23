#include "render_example.hpp"

int main(int argc, char **argv) {
    return render_example(ure::client::TransportMode::Direct, argc, argv);
}
