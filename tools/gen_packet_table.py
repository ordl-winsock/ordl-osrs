#!/usr/bin/env python3
"""
gen_packet_table.py - Extract OSRS server packet size table from rsprot source.

Usage:
    python3 tools/gen_packet_table.py /path/to/rsprot/protocol/osrs-XXX/osrs-XXX-desktop

Generates C code for osrs_server_packet_sizes array that can be pasted into protocol.c.
"""

import sys
import re
from pathlib import Path


def parse_rsprot(prot_dir: Path):
    """Parse rsprot GameServerProt.kt and GameServerProtId.kt files."""
    prot_file = prot_dir / "src/main/kotlin/net/rsprot/protocol/game/outgoing/prot/GameServerProt.kt"
    id_file = prot_dir / "src/main/kotlin/net/rsprot/protocol/game/outgoing/prot/GameServerProtId.kt"

    if not prot_file.exists():
        raise FileNotFoundError(f"GameServerProt.kt not found at {prot_file}")
    if not id_file.exists():
        raise FileNotFoundError(f"GameServerProtId.kt not found at {id_file}")

    # Parse ID -> opcode mapping
    id_to_opcode = {}
    with open(id_file) as f:
        for line in f:
            m = re.search(r'const val ([A-Z_\d]+)\s*=\s*(\d+)', line)
            if m:
                id_to_opcode[m.group(1)] = int(m.group(2))

    # Parse enum entries: NAME(GameServerProtId.ID_NAME, size)
    packets = []
    with open(prot_file) as f:
        for line in f:
            m = re.search(r'([A-Z_\d]+)\(GameServerProtId\.([A-Z_\d]+),\s*(\S+)\)', line)
            if m:
                name = m.group(1)
                id_name = m.group(2)
                size_str = m.group(3)
                if size_str == 'Prot.VAR_SHORT':
                    size = 'VS'
                elif size_str == 'Prot.VAR_BYTE':
                    size = 'VB'
                else:
                    size = size_str
                if id_name in id_to_opcode:
                    opcode = id_to_opcode[id_name]
                    packets.append((opcode, size, name))

    # Sort by opcode
    packets.sort(key=lambda x: x[0])
    return packets


def generate_c_code(packets, revision=""):
    """Generate C code for osrs_server_packet_sizes array."""
    lines = []
    lines.append("  /* Server→client packet sizes")
    lines.append(f"   * Generated from rsprot revision {revision}")
    lines.append("   * Use tools/gen_packet_table.py to regenerate")
    lines.append("   */")
    lines.append("  {")
    lines.append("    int16_t *dst = osrs_server_packet_sizes;")
    lines.append("    for (int i = 0; i < OSRS_SERVER_PACKET_COUNT; i++)")
    lines.append("      dst[i] = XX;")
    lines.append("    (void)dst;")

    for opcode, size, name in packets:
        comment = f"    /* {name} */"
        lines.append(f"    osrs_server_packet_sizes[{opcode}] = {size};  {comment}")

    lines.append("  }")
    return "\n".join(lines)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} /path/to/rsprot/protocol/osrs-XXX/osrs-XXX-desktop")
        sys.exit(1)

    prot_dir = Path(sys.argv[1])
    if not prot_dir.is_dir():
        print(f"Error: {prot_dir} is not a directory")
        sys.exit(1)

    # Extract revision from path
    rev = "unknown"
    for part in prot_dir.parts:
        if part.startswith("osrs-"):
            rev = part
            break

    packets = parse_rsprot(prot_dir)
    print(f"Parsed {len(packets)} packets from rsprot {rev}")
    print()
    print(generate_c_code(packets, rev))


if __name__ == "__main__":
    main()
