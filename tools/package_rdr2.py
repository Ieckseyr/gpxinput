#!/usr/bin/env python3
# pkg
# 打包 + 刷新产物
# 只收白名单里的文件

import io
import json
import os
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(ROOT, "dist")
SRC  = os.path.join(DIST, "rdr2_haptics")

# 包内名->来源
ARTIFACTS = {
    "xinput9_1_0.dll": os.path.join(ROOT, "build", "proxy", "xinput9_1_0", "xinput9_1_0.dll"),
    "xinput1_4.dll":   os.path.join(ROOT, "build", "proxy", "xinput1_4",   "xinput1_4.dll"),
    "xinput1_3.dll":   os.path.join(ROOT, "build", "proxy", "xinput1_3",   "xinput1_3.dll"),
    "gpxinput_rdr2.asi": os.path.join(ROOT, "build", "bin", "gpxinput_rdr2.asi"),
}

# 白名单
ALLOW = ["README.md", "gpxinput.ini", "mod.json", "震动调参速查.md",
         "xinput9_1_0.dll", "xinput1_4.dll", "xinput1_3.dll", "gpxinput_rdr2.asi"]


def main():
    with io.open(os.path.join(SRC, "mod.json"), encoding="utf-8") as f:
        ver = json.load(f)["version"]
    name = "gpxinput_rdr2_haptics_v" + ver

    for dst_name, src in ARTIFACTS.items():
        if not os.path.isfile(src):
            print("[失败] 缺少构建产物: " + src)
            return 1
        shutil.copy2(src, os.path.join(SRC, dst_name))
        print("刷新 %s" % dst_name)

    missing = [f for f in ALLOW if not os.path.isfile(os.path.join(SRC, f))]
    if missing:
        print("[失败] dist/rdr2_haptics/ 缺少: " + ", ".join(missing))
        return 1

    for old in os.listdir(DIST):
        if old.startswith("gpxinput_rdr2_haptics_v") and old.endswith(".zip"):
            try:
                os.remove(os.path.join(DIST, old))
                print("删除旧包 " + old)
            except OSError:
                # 旧包被资源管理器/解压工具占着是常事，删不掉就留着：
                # 它是上一个版本，不影响这次打包，没必要让打包失败。
                print("旧包被占用，跳过删除 " + old)

    zip_path = os.path.join(DIST, name + ".zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(ALLOW):
            z.write(os.path.join(SRC, f), name + "/" + f)

    print("")
    with zipfile.ZipFile(zip_path) as z:
        for i in z.infolist():
            print("  %-52s %8d" % (i.filename, i.file_size))
    print("")
    print("%s.zip  %d 个文件 %d 字节" % (name, len(ALLOW), os.path.getsize(zip_path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
