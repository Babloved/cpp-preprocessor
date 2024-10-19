#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <set>

using namespace std;
using filesystem::path;
const int BUFF_SIZE = 1024;

path operator ""_p(const char *data, std::size_t sz) {
    return path(data, data + sz);
}
void GetListLibPath(map<string, path> &list_path, const path &p, int offset = 0) {
    set<path, greater<>> files;
    string p_str = p.string();
    string offset_string(offset * 2, ' ');
    if (status(p).type() == filesystem::file_type::regular) {
        list_path[p.filename().string()] = p;
        return;
    }
    for (const auto &dir_entry: filesystem::directory_iterator(p)) {
        files.insert(dir_entry.path().filename());

    }
    for (const auto &el: files) {
        GetListLibPath(list_path, p / el, offset + 1);
    }
}


bool Replace(const path &in_file, ofstream &dst, const map<string, path> &list_path) {
    ifstream src(in_file);
    if (!src.is_open()) {
        return false;
    }
    auto source_path = in_file.parent_path();
    static regex include_local_reg(R"/(\s*#\s*include\s*"([^"]*)"\s*)/");
    static regex include_base_reg(R"/(\s*#\s*include\s*<([^>]*)>\s*)/");
    string line_buff;
    line_buff.reserve(BUFF_SIZE);
    smatch smatch_base;
    smatch smatch_local;
    unsigned int line = 1;
    //We go through the source file line by line and insert the appropriate library
    while (getline(src, line_buff)) {
        regex_match(line_buff, smatch_local, include_local_reg);
        if (smatch_local.empty()) {
            regex_match(line_buff, smatch_base, include_base_reg);
            if (smatch_base.empty()) {
                //Not contains include
                line_buff.push_back('\n');
                dst.write(line_buff.data(), line_buff.size());
            } else {
                //Contains base include
                path lib = static_cast<string>(smatch_base[1]);
//                cout << "Find base lib file: " << lib << endl;

                auto iterator_path = list_path.find(lib.filename().string());
                path lib_path = (iterator_path != list_path.end()) ? (*iterator_path).second : "";

                ifstream lib_file(lib_path);
                if (!exists(lib_path)) {
                    cout << "unknown include file " << lib.generic_string() << " at file " << in_file.generic_string()
                         << " at line " << line << endl;
                    return false;
                }
                if (!Replace(lib_path,dst,list_path)){
                    return false;
                }
            }
        } else {
            //Contains local include
            path lib = static_cast<string>(smatch_local[1]);
            path lib_path = source_path / lib;
//            cout << "Find local lib file: " << lib << endl;
            if (!exists(lib_path)) {
                auto iterator_path = list_path.find(lib.filename().string());
                lib_path = (iterator_path != list_path.end()) ? (*iterator_path).second : "";
                if (!exists(lib_path)){
                    cout << "unknown include file " << lib.generic_string() << " at file " << in_file.generic_string() << " at line "
                            << line << endl;
                    return false;
                }
            }
            lib_path.make_preferred();
            if (!Replace(lib_path,dst,list_path)){
                return false;
            }
        }
        line++;
    }
    return true;
}

bool Preprocess(const path &in_file, const path &out_file, const vector<path> &include_directories) {

    ofstream dst(out_file);
    if (!dst.is_open()) {
        return false;
    }

    // We get a recursive list of all libraries
    map<string, path> list_path;
    for (const auto &el: include_directories) {
        if (!exists(el)){
            continue;
        }
        GetListLibPath(list_path, el);
    }
    return Replace(in_file, dst, list_path);
}

string GetFileContents(string file) {
    ifstream stream(file);

    // конструируем string по двум итераторам
    return {(istreambuf_iterator<char>(stream)), istreambuf_iterator<char>()};
}

void Test() {
    error_code err;
    filesystem::remove_all("sources"_p, err);
    filesystem::create_directories("sources"_p / "include2"_p / "lib"_p, err);
    filesystem::create_directories("sources"_p / "include1"_p, err);
    filesystem::create_directories("sources"_p / "dir1"_p / "subdir"_p, err);

    {
        ofstream file("sources/a.cpp");
        file << "// this comment before include\n"
                "#include \"dir1/b.h\"\n"
                "// text between b.h and c.h\n"
                "#include \"dir1/d.h\"\n"
                "\n"
                "int SayHello() {\n"
                "    cout << \"hello, world!\" << endl;\n"
                "#   include<dummy.txt>\n"
                "}\n"s;
    }
    {
        ofstream file("sources/dir1/b.h");
        file << "// text from b.h before include\n"
                "#include \"subdir/c.h\"\n"
                "// text from b.h after include"s;
    }
    {
        ofstream file("sources/dir1/subdir/c.h");
        file << "// text from c.h before include\n"
                "#include <std1.h>\n"
                "// text from c.h after include\n"s;
    }
    {
        ofstream file("sources/dir1/d.h");
        file << "// text from d.h before include\n"
                "#include \"lib/std2.h\"\n"
                "// text from d.h after include\n"s;
    }
    {
        ofstream file("sources/include1/std1.h");
        file << "// std1\n"s;
    }
    {
        ofstream file("sources/include2/lib/std2.h");
        file << "// std2\n"s;
    }

    assert((!Preprocess("sources"_p / "a.cpp"_p, "sources"_p / "a.in"_p,
                        {"sources"_p / "include1"_p,"sources"_p / "include2"_p})));

    ostringstream test_out;
    test_out << "// this comment before include\n"
                "// text from b.h before include\n"
                "// text from c.h before include\n"
                "// std1\n"
                "// text from c.h after include\n"
                "// text from b.h after include\n"
                "// text between b.h and c.h\n"
                "// text from d.h before include\n"
                "// std2\n"
                "// text from d.h after include\n"
                "\n"
                "int SayHello() {\n"
                "    cout << \"hello, world!\" << endl;\n"s;

    assert(GetFileContents("sources/a.in"s) == test_out.str());
}


int main() {
    Test();
}