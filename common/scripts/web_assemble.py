import gzip
import json
import os
import shutil

Import("env")


FS_TARGETS = {"buildfs", "uploadfs", "uploadfsota"}


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
