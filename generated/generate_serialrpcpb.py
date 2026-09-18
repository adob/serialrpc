#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys


def main() -> None:
    outdir = Path(sys.argv[1])
    outdir.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            "protoc",
            f"--cpp_out={outdir}",
            "-I",
            "..",
            "../serialrpc.proto",
        ],
        check=True,
    )
    (outdir / "serialrpcpb.cc").write_text(
        'export module serialrpc.generated.serialrpcpb;\n'
        'export import "serialrpc.pb.h";\n'
    )


if __name__ == "__main__":
    main()
