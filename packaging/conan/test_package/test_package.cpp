#include "signet/forge.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

int main() {
    namespace sf = signet::forge;
    const std::string path = "conan_test.parquet";

    // Define schema
    auto schema = sf::Schema::build("test",
        sf::Column<int32_t>{"id"},
        sf::Column<std::string>{"name"});

    // Write a trivial 2-row Parquet file
    {
        auto writer = sf::ParquetWriter::open(path, schema);
        if (!writer) {
            std::fprintf(stderr, "Writer open failed\n");
            return EXIT_FAILURE;
        }
        (void)writer->write_row({"1", "Alice"});
        (void)writer->write_row({"2", "Bob"});
        auto cs = writer->close();
        if (!cs) {
            std::fprintf(stderr, "Writer close failed\n");
            return EXIT_FAILURE;
        }
    }

    // Read it back and verify
    {
        auto reader = sf::ParquetReader::open(path);
        if (!reader) {
            std::fprintf(stderr, "Reader open failed\n");
            return EXIT_FAILURE;
        }
        if (reader->num_rows() != 2) {
            std::fprintf(stderr, "Expected 2 rows, got %lld\n",
                         static_cast<long long>(reader->num_rows()));
            return EXIT_FAILURE;
        }
    }

    std::filesystem::remove(path);
    std::puts("signet_forge Conan test: OK");
    return EXIT_SUCCESS;
}
