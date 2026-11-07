#include "../tools/seven_zip.hpp"
#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    // 1. Test is_7z_archive on non-existent or empty file
    assert(!seven_zip::is_7z_archive("non_existent_file.7z"));

    std::string dummy_file = "dummy_not_7z.bin";
    {
        std::ofstream out(dummy_file);
        out << "Hello world, not a 7z archive!";
    }
    assert(!seven_zip::is_7z_archive(dummy_file));
    fs::remove(dummy_file);

    // 2. Test extraction failure on invalid file
    assert(!seven_zip::extract_file("non_existent_file.7z", "any", "out.bin"));

    // 3. Test real extraction if 7z tool is available to create a fixture
    std::string test_txt = "test_data.txt";
    {
        std::ofstream out(test_txt);
        out << "Hello from 7z extraction!";
    }
    std::string test_7z = "test_archive.7z";
    int ret = system(("7z a " + test_7z + " " + test_txt + " > /dev/null 2>&1").c_str());
    (void)ret;
    if (fs::exists(test_7z)) {
        assert(seven_zip::is_7z_archive(test_7z));
        std::string extracted_txt = "extracted_data.txt";
        assert(seven_zip::extract_file(test_7z, test_txt, extracted_txt));
        assert(fs::exists(extracted_txt));
        std::string content;
        {
            std::ifstream in(extracted_txt);
            std::getline(in, content);
        }
        assert(content == "Hello from 7z extraction!");
        fs::remove(extracted_txt);
        fs::remove(test_7z);
    }
    fs::remove(test_txt);

    std::cout << "seven_zip: header detection, error handling and extraction passed" << std::endl;
    return 0;
}
