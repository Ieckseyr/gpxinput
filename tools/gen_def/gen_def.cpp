// tool
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#include "gp_text.h"

static std::string g_prefix = "gp_exp_";

struct Export {
    std::string name;
    WORD ordinal;
};





static bool DumpExports(const wchar_t* path, std::vector<Export>& out, std::string& err) {
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        err = "CreateFile failed";
        return false;
    }

    HANDLE hMap = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hMap) {
        CloseHandle(hFile);
        err = "CreateFileMapping failed";
        return false;
    }

    const BYTE* base = (const BYTE*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!base) {
        CloseHandle(hMap);
        CloseHandle(hFile);
        err = "MapViewOfFile failed";
        return false;
    }

    bool ok = false;
    do {
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) { err = "not a PE (bad MZ)"; break; }

        const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) { err = "not a PE (bad PE sig)"; break; }

        const IMAGE_DATA_DIRECTORY& dir =
            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (dir.VirtualAddress == 0 || dir.Size == 0) { err = "no export directory"; break; }

        
        auto rvaToPtr = [&](DWORD rva) -> const BYTE* {
            const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
            for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
                if (rva >= sec->VirtualAddress &&
                    rva < sec->VirtualAddress + sec->Misc.VirtualSize) {
                    return base + sec->PointerToRawData + (rva - sec->VirtualAddress);
                }
            }
            return nullptr;
        };

        const IMAGE_EXPORT_DIRECTORY* exp =
            (const IMAGE_EXPORT_DIRECTORY*)rvaToPtr(dir.VirtualAddress);
        if (!exp) { err = "export table outside sections"; break; }

        const DWORD* names    = (const DWORD*)rvaToPtr(exp->AddressOfNames);
        const WORD* ordinals  = (const WORD*)rvaToPtr(exp->AddressOfNameOrdinals);
        const DWORD* functions= (const DWORD*)rvaToPtr(exp->AddressOfFunctions);

        
        
        std::vector<bool> named(exp->NumberOfFunctions, false);

        if (names && ordinals) {
            for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
                const char* n = (const char*)rvaToPtr(names[i]);
                if (!n) continue;
                WORD idx = ordinals[i];
                if (idx >= exp->NumberOfFunctions) continue;
                named[idx] = true;
                Export e;
                e.name = n;
                e.ordinal = (WORD)(exp->Base + idx);
                out.push_back(e);
            }
        }

        for (DWORD i = 0; i < exp->NumberOfFunctions; ++i) {
            if (named[i]) continue;
            if (functions && functions[i] == 0) continue;  
            Export e;
            e.name.clear();  
            e.ordinal = (WORD)(exp->Base + i);
            out.push_back(e);
        }

        ok = true;
    } while (false);

    UnmapViewOfFile(base);
    CloseHandle(hMap);
    CloseHandle(hFile);
    return ok;
}















static bool DumpImports(const wchar_t* path, FILE* out, std::string& err) {
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) { err = "CreateFile failed"; return false; }

    HANDLE hMap = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hMap) { CloseHandle(hFile); err = "CreateFileMapping failed"; return false; }

    const BYTE* base = (const BYTE*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!base) { CloseHandle(hMap); CloseHandle(hFile); err = "MapViewOfFile failed"; return false; }

    bool ok = false;
    do {
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) { err = "not a PE"; break; }
        const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) { err = "not a PE"; break; }

        const IMAGE_DATA_DIRECTORY& dir =
            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (dir.VirtualAddress == 0) { err = "no import directory"; break; }

        auto rvaToPtr = [&](DWORD rva) -> const BYTE* {
            const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
            for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
                if (rva >= sec->VirtualAddress &&
                    rva < sec->VirtualAddress + sec->Misc.VirtualSize) {
                    return base + sec->PointerToRawData + (rva - sec->VirtualAddress);
                }
            }
            return nullptr;
        };

        const IMAGE_IMPORT_DESCRIPTOR* imp =
            (const IMAGE_IMPORT_DESCRIPTOR*)rvaToPtr(dir.VirtualAddress);
        if (!imp) { err = "import table outside sections"; break; }

        const bool is64 = nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;

        for (; imp->Name != 0; ++imp) {
            const char* dllName = (const char*)rvaToPtr(imp->Name);
            if (!dllName) continue;

            
            DWORD thunkRva = imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk;
            const BYTE* thunk = rvaToPtr(thunkRva);
            if (!thunk) continue;

            fprintf(out, "\n%s\n", dllName);

            int named = 0, byOrdinal = 0;
            for (int k = 0; k < 4096; ++k) {
                ULONGLONG entry = is64
                    ? ((const ULONGLONG*)thunk)[k]
                    : (ULONGLONG)((const DWORD*)thunk)[k];
                if (entry == 0) break;

                ULONGLONG ordinalFlag = is64 ? 0x8000000000000000ull : 0x80000000ull;
                if (entry & ordinalFlag) {
                    fprintf(out, "    序号 %llu\n", entry & 0xFFFF);
                    ++byOrdinal;
                } else {
                    const IMAGE_IMPORT_BY_NAME* ibn =
                        (const IMAGE_IMPORT_BY_NAME*)rvaToPtr((DWORD)entry);
                    if (ibn) {
                        fprintf(out, "    %s\n", (const char*)ibn->Name);
                        ++named;
                    }
                }
            }
            fprintf(out, "    -- 共 %d 个具名 + %d 个按序号\n", named, byOrdinal);
        }
        ok = true;
    } while (false);

    UnmapViewOfFile(base);
    CloseHandle(hMap);
    CloseHandle(hFile);
    return ok;
}

int wmain(int argc, wchar_t** argv) {
    




    if (argc >= 3 && !_wcsicmp(argv[1], L"--imports")) {
        FILE* out = stdout;
        if (argc >= 4) {
            if (_wfopen_s(&out, argv[3], L"w") != 0 || !out) {
                fwprintf(stderr, L"gen_def: 无法写入 %ls\n", argv[3]);
                return 1;
            }
        }
        std::string err;
        bool ok = DumpImports(argv[2], out, err);
        if (out != stdout) fclose(out);
        if (!ok) {
            fwprintf(stderr, L"gen_def: 读导入表失败: %hs\n", err.c_str());
            return 1;
        }
        return 0;
    }

    if (argc < 3) {
        fwprintf(stderr, L"usage: gen_def <dll-path> <output.def> [wrapper-prefix]\n");
        fwprintf(stderr, L"       gen_def --imports <exe-or-dll> [out.txt]\n");
        return 2;
    }
    if (argc >= 4) {
        char buf[256] = {0};
        WideCharToMultiByte(CP_ACP, 0, argv[3], -1, buf, sizeof(buf) - 1, nullptr, nullptr);
        g_prefix = buf;
    }

    std::vector<Export> exports;
    std::string err;
    if (!DumpExports(argv[1], exports, err)) {
        fwprintf(stderr, L"gen_def: cannot read %ls: %hs\n", argv[1], err.c_str());
        return 1;
    }
    if (exports.empty()) {
        fwprintf(stderr, L"gen_def: %ls exports nothing\n", argv[1]);
        return 1;
    }

    
    std::sort(exports.begin(), exports.end(),
              [](const Export& a, const Export& b) { return a.ordinal < b.ordinal; });

    
    
    std::wstring libName = argv[1];
    size_t slash = libName.find_last_of(L"\\/");
    if (slash != std::wstring::npos) libName = libName.substr(slash + 1);
    size_t dot = libName.find_last_of(L'.');
    if (dot != std::wstring::npos) libName = libName.substr(0, dot);

    FILE* f = nullptr;
    if (_wfopen_s(&f, argv[2], L"w") != 0 || !f) {
        fwprintf(stderr, L"gen_def: cannot write %ls\n", argv[2]);
        return 1;
    }

    


    char libNameUtf8[MAX_PATH * 3] = {0};
    GpWideToUtf8(libName.c_str(), libNameUtf8, sizeof(libNameUtf8));

    fprintf(f, "; AUTO-GENERATED by tools/gen_def -- do not edit by hand.\n");
    fprintf(f, "; Mirrors the export table of %%SystemRoot%%\\System32\\%s.dll\n", libNameUtf8);
    fprintf(f, ";\n");
    fprintf(f, "; Regenerate with:\n");
    fprintf(f, ";   gen_def %%SystemRoot%%\\System32\\%s.dll proxy\\%s.def\n",
            libNameUtf8, libNameUtf8);
    fprintf(f, "\nLIBRARY %s\nEXPORTS\n", libNameUtf8);

    int namedCount = 0, ordinalCount = 0;
    for (const Export& e : exports) {
        if (e.name.empty()) {
            
            
            fprintf(f, "    %sordinal%d @%d NONAME\n", g_prefix.c_str(), e.ordinal, e.ordinal);
            ++ordinalCount;
        } else {
            fprintf(f, "    %s=%s%s @%d\n", e.name.c_str(), g_prefix.c_str(),
                    e.name.c_str(), e.ordinal);
            ++namedCount;
        }
    }

    fclose(f);

    wprintf(L"gen_def: %ls -> %ls  (%d named, %d ordinal-only)\n",
            argv[1], argv[2], namedCount, ordinalCount);
    return 0;
}
