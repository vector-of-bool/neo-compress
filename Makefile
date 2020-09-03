.SILENT:
.PHONY: build prep gcc-10 gcc-9

build: gcc-10

prep:
	dds catalog import --json pkgs-cat.jsonc

gcc-10: prep
	dds build -t tools/gcc-10.jsonc

gcc-9: prep
	dds build -t tools/gcc-9.jsonc
