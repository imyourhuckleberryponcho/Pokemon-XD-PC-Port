#include "../src/dol_tables.h"

#include <port_disc.h>

#include <stdio.h>
#include <string.h>

static void print_file(const char* path, uint32_t size, void* user) {
  (void)user;
  printf("%10u  %s\n", size, path);
}

static int extract(const char* path, const char* output) {
  size_t size = 0;
  uint8_t* data = PortDisc_ReadFile(path, &size);
  FILE* f = data != NULL ? fopen(output, "wb") : NULL;
  if (f == NULL || fwrite(data, 1, size, f) != size) {
    fprintf(stderr, "FAILED: could not extract %s\n", path);
    return 1;
  }
  fclose(f);
  PortDisc_Free(data);
  printf("Extracted %s (%zu bytes)\n", path, size);
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <disc image> [--dump <folder> | --list | --extract <path> <output>]\n", argv[0]);
    return 2;
  }
  const char* dumpDir = (argc >= 4 && strcmp(argv[2], "--dump") == 0) ? argv[3] : NULL;

  char error[512];
  if (!PortDisc_Open(argv[1], error, sizeof(error))) {
    fprintf(stderr, "FAILED: %s\n", error);
    return 1;
  }
  if (argc >= 3 && strcmp(argv[2], "--list") == 0) {
    PortDisc_ListFiles(print_file, NULL);
    return 0;
  }
  if (argc >= 5 && strcmp(argv[2], "--extract") == 0) {
    return extract(argv[3], argv[4]);
  }

  size_t dolSize = 0;
  const uint8_t* dol = PortDisc_Dol(&dolSize);
  char sha1[41];
  PortSha1Hex(dol, dolSize, sha1);
  printf("Game ID:       %s\n", PortDisc_GameId());
  printf("main.dol:      %zu bytes, SHA-1 %s\n", dolSize, sha1);

  if (!PortXD_LoadDolTables(dol, dolSize, error, sizeof(error))) {
    fprintf(stderr, "FAILED: %s\n", error);
    return 1;
  }
  printf("Version check: OK (USA, GXXE01)\n");
  for (int i = 0; i < PortXD_DolTableCount(); i++) {
    const char* name = PortXD_DolTableName(i);
    const uint32_t size = PortXD_DolTableSize(i);
    const uint8_t* data = PortXD_GetDolTable(name);
    PortSha1Hex(data, size, sha1);
    printf("  %-17s %7u bytes  sha1 %s\n", name, size, sha1);
    if (dumpDir != NULL) {
      char path[1024];
      snprintf(path, sizeof(path), "%s/%s.bin", dumpDir, name);
      FILE* f = fopen(path, "wb");
      if (f == NULL || fwrite(data, 1, size, f) != size) {
        fprintf(stderr, "FAILED: could not write %s\n", path);
        return 1;
      }
      fclose(f);
    }
  }
  PortDisc_Close();
  printf("All tables loaded.\n");
  return 0;
}
