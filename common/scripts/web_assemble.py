import gzip
import json
import os
import shutil

Import("env")


FS_TARGETS = {"buildfs", "uploadfs", "uploadfsota"}

# LittleFS budget (D14): the SPA must fit the 128 KiB min_spiffs partition
# with room to spare. Both limits are checked after every assemble.
WEB_GZ_BUDGET = 65536       # sum of the gzipped file sizes
WEB_BLOCK_BUDGET = 102400   # sum of the file sizes rounded up to 4 KiB blocks
FS_BLOCK = 4096

# Language files (D13): flat objects of non-empty strings, identical keys.
LANG_FILES = ("lang/en.json", "lang/uk.json")


def _iter_files(root):
    # Yields (relpath, abspath) for every visible file under root.
    # Hidden files/dirs (name starts with ".") are skipped. relpaths use "/".
    if not os.path.isdir(root):
        return
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if not d.startswith(".")]
        for name in filenames:
            if name.startswith("."):
                continue
            abspath = os.path.join(dirpath, name)
            relpath = os.path.relpath(abspath, root).replace(os.sep, "/")
            yield relpath, abspath


def _write_gz(dst_path, src_path):
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    if src_path.endswith(".gz"):
        shutil.copyfile(src_path, dst_path)
        return
    with open(src_path, "rb") as src, open(dst_path, "wb") as raw_out:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw_out, compresslevel=9, mtime=0) as gz_out:
            gz_out.write(src.read())


def _load_lang(relpath, abspath):
    # Returns (dict, None) or (None, error text).
    try:
        with open(abspath, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError) as e:
        return None, "%s: cannot parse (%s)" % (relpath, e)
    if not isinstance(data, dict):
        return None, "%s: not a JSON object" % (relpath,)
    for key, value in data.items():
        if not isinstance(value, str):
            return None, "%s: key %r is not a string (files must be flat)" % (relpath, key)
        if value == "":
            return None, "%s: key %r is empty" % (relpath, key)
    return data, None


def check_lang_parity(entries):
    # entries: relpath -> source abspath (after project overrides). Returns
    # True when both language files exist, are valid and have the same keys.
    loaded = []
    for relpath in LANG_FILES:
        if relpath not in entries:
            print("web_assemble: error: %s is missing" % (relpath,))
            return False
        data, err = _load_lang(relpath, entries[relpath])
        if err is not None:
            print("web_assemble: error: " + err)
            return False
        loaded.append(data)
    a, b = loaded
    for key in a:
        if key not in b:
            print("web_assemble: error: key %r is in %s but missing in %s" % (key, LANG_FILES[0], LANG_FILES[1]))
            return False
    for key in b:
        if key not in a:
            print("web_assemble: error: key %r is in %s but missing in %s" % (key, LANG_FILES[1], LANG_FILES[0]))
            return False
    print("web_assemble: lang parity OK (%d keys)" % (len(a),))
    return True


def check_budget(data_dir):
    # Sums the stored (gzipped) size of every generated file and the same
    # sizes rounded up to whole LittleFS blocks. Returns True within budget.
    total = 0
    blocks = 0
    for _relpath, abspath in _iter_files(data_dir):
        size = os.path.getsize(abspath)
        total += size
        blocks += ((size + FS_BLOCK - 1) // FS_BLOCK) * FS_BLOCK
    print("web_assemble: gz total %d / %d bytes" % (total, WEB_GZ_BUDGET))
    print("web_assemble: block total %d / %d bytes" % (blocks, WEB_BLOCK_BUDGET))
    ok = True
    if total > WEB_GZ_BUDGET:
        print("web_assemble: error: gz total exceeds the budget by %d bytes" % (total - WEB_GZ_BUDGET,))
        ok = False
    if blocks > WEB_BLOCK_BUDGET:
        print("web_assemble: error: block total exceeds the budget by %d bytes" % (blocks - WEB_BLOCK_BUDGET,))
        ok = False
    return ok


def assemble(common_web, project_web, data_dir, project_name, version):
    # Refuses (prints an error, returns -1, deletes nothing) unless data_dir is
    # clearly a generated "data" directory inside the project. This guards
    # against a bad $PROJECT_DATA_DIR ever triggering an rmtree elsewhere.
    if os.path.basename(os.path.normpath(data_dir)) != "data":
        print("web_assemble: refusing to touch %r (not a 'data' dir)" % (data_dir,))
        return -1
    parent = os.path.dirname(os.path.normpath(data_dir))
    if os.path.basename(parent) != project_name:
        print("web_assemble: refusing to touch %r (parent is not %r)" % (data_dir, project_name))
        return -1

    if os.path.isdir(data_dir):
        shutil.rmtree(data_dir)
    os.makedirs(data_dir, exist_ok=True)

    if not os.path.isdir(common_web):
        print("web_assemble: warning: common web dir %r not found; using project files only" % (common_web,))

    entries = {}
    for relpath, abspath in _iter_files(common_web):
        entries[relpath] = abspath
    for relpath, abspath in _iter_files(project_web):
        entries[relpath] = abspath

    # Generated files always win over a web source with the same name.
    generated = ("version.json", "version.txt")
    for name in generated:
        if name in entries:
            print("web_assemble: warning: a web source maps to %r; the generated file wins" % (name,))

    count = 0
    for relpath, abspath in entries.items():
        if relpath in generated:
            continue
        dst = os.path.join(data_dir, relpath + ".gz")
        _write_gz(dst, abspath)
        count += 1

    version_payload = json.dumps({"project": project_name, "fw": version}).encode("utf-8")
    version_dst = os.path.join(data_dir, "version.json.gz")
    with open(version_dst, "wb") as raw_out:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw_out, compresslevel=9, mtime=0) as gz_out:
            gz_out.write(version_payload)
    count += 1

    # Plain (uncompressed) web image version for the firmware's mismatch check
    # (stage 04, D19): read once at boot by ConnectivityServices, no inflate.
    with open(os.path.join(data_dir, "version.txt"), "wb") as txt_out:
        txt_out.write((version + "\n").encode("utf-8"))
    count += 1

    print("web_assemble: %d files -> %s" % (count, data_dir))

    # Checks run on the assembled image; a failure returns -1 so the build
    # stops (the generated data/ is left in place for inspection).
    if not check_lang_parity(entries):
        return -1
    if not check_budget(data_dir):
        return -1
    return count


if FS_TARGETS & set(str(t) for t in COMMAND_LINE_TARGETS):
    project_dir = env["PROJECT_DIR"]
    common_web_dir = os.path.normpath(os.path.join(project_dir, "..", "common", "web"))
    project_web_dir = os.path.join(project_dir, "web")
    data_dir = env.subst("$PROJECT_DATA_DIR")
    project_name = os.path.basename(os.path.normpath(project_dir))
    fw_version = env.get("HEATER_FW_VERSION", "unknown")

    result = assemble(common_web_dir, project_web_dir, data_dir, project_name, fw_version)
    if result == -1:
        env.Exit(1)
