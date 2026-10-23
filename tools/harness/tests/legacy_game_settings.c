#include "harness.h"
/* network_game_layout.c's network_game_layout_from_version: an older host's
game settings record made this version's (network version 36's) on a client
of the fork. A host before 35 sends the gametype's PC options' first 0x1C
bytes, the record 16 bytes shorter; a host before 33 has no ALLY FRIENDLY
FIRE bit. Records at the PCs' slots (128 and 128) and the console's (16 and
16): what is checked is the tail, after the players. */
#include "network_game_layout.h"
#include "under_test.inc"

#define SLOTS_PC 128
#define SLOTS_CONSOLE 16
#define RECORD_MAXIMUM 0x4000
static unsigned char record[RECORD_MAXIMUM];
static unsigned char sent[RECORD_MAXIMUM];

/* an older host's record: a header, machines and players of a pattern,
co-op's flags (both switches off: 0x03), then the tail: 10 bytes, the
0x1C bytes of PC options, the local data (4 bytes) */
static unsigned long legacy_record(int slots)
{
 unsigned long size=network_game_layout_size(slots,slots)-(NETWORK_GAME_LAYOUT_TAIL_SIZE-NETWORK_GAME_LAYOUT_LEGACY_TAIL_SIZE);
 unsigned char *tail=record+size-NETWORK_GAME_LAYOUT_LEGACY_TAIL_SIZE;
 unsigned long index;
 memset(record,0xEE,sizeof(record));
 for(index=0;index<size;index++)record[index]=(unsigned char)(index*7+3);
 record[NETWORK_GAME_LAYOUT_COOPERATIVE_FLAGS_OFFSET]=0x03;
 for(index=0;index<NETWORK_GAME_LAYOUT_LEGACY_OPTIONS_SIZE;index++)
  tail[NETWORK_GAME_LAYOUT_TAIL_OPTIONS+index]=(unsigned char)(0x40+index);
 memcpy(tail+NETWORK_GAME_LAYOUT_TAIL_OPTIONS+NETWORK_GAME_LAYOUT_LEGACY_OPTIONS_SIZE,"\xAA\xBB\xCC\xDD",4);
 memcpy(sent,record,sizeof(record));
 return size;
}

/* the widened record: what came before the options as sent, the old options
kept, OpenCE's zeroed, the local data after them */
static int widened_ok(int slots,unsigned char flags)
{
 unsigned long size=network_game_layout_size(slots,slots);
 unsigned long legacy=size-(NETWORK_GAME_LAYOUT_TAIL_SIZE-NETWORK_GAME_LAYOUT_LEGACY_TAIL_SIZE);
 unsigned char const *options=record+size-NETWORK_GAME_LAYOUT_TAIL_SIZE+NETWORK_GAME_LAYOUT_TAIL_OPTIONS;
 unsigned long index;
 unsigned long before=legacy-NETWORK_GAME_LAYOUT_LEGACY_TAIL_SIZE+NETWORK_GAME_LAYOUT_TAIL_OPTIONS;
 for(index=0;index<before;index++)
  if(index!=NETWORK_GAME_LAYOUT_COOPERATIVE_FLAGS_OFFSET&&record[index]!=sent[index])return 0;
 if(record[NETWORK_GAME_LAYOUT_COOPERATIVE_FLAGS_OFFSET]!=flags)return 0;
 for(index=0;index<NETWORK_GAME_LAYOUT_LEGACY_OPTIONS_SIZE;index++)if(options[index]!=0x40+index)return 0;
 for(;index<NETWORK_GAME_LAYOUT_OPTIONS_SIZE;index++)if(options[index]!=0)return 0;
 if(memcmp(options+NETWORK_GAME_LAYOUT_OPTIONS_SIZE,"\xAA\xBB\xCC\xDD",4))return 0;
 /* (nothing past the record) */
 return record[size]==0xEE;
}

int main(int argc,char **argv)
{
 const char *case_name=argc>1?argv[1]:"";
 unsigned long pc=network_game_layout_size(SLOTS_PC,SLOTS_PC);
 CHECK(pc==13136,"this version's record at the PCs' slots, %lu",pc);
 CHECK(network_game_layout_size(SLOTS_CONSOLE,SLOTS_CONSOLE)==1936,"at the console's");
 CASE("legacy") {
  /* a host of 34 (no OpenCE gametype; the ally bit its own) */
  unsigned long legacy=legacy_record(SLOTS_PC);
  CHECK(legacy==13120,"an older host's record, %lu",legacy);
  CHECK(network_game_layout_from_version(record,legacy,pc,34)==NETWORK_GAME_LAYOUT_OK,"taken");
  CHECK(widened_ok(SLOTS_PC,0x03),"widened, the ally bit kept");
 } else CASE("legacy_ally") {
  /* a host of 32: no ALLY FRIENDLY FIRE (on), the collisions bit kept */
  unsigned long legacy=legacy_record(SLOTS_PC);
  CHECK(network_game_layout_from_version(record,legacy,pc,32)==NETWORK_GAME_LAYOUT_OK,"taken");
  CHECK(widened_ok(SLOTS_PC,0x01),"widened, ALLY FRIENDLY FIRE on");
  legacy=legacy_record(SLOTS_PC);
  CHECK(network_game_layout_from_version(record,legacy,pc,11)==NETWORK_GAME_LAYOUT_OK,"a host of 11");
  CHECK(widened_ok(SLOTS_PC,0x01),"a host of 11's");
 } else CASE("legacy_unknown") {
  /* a host not known (an invitation's), or announcing 35 or later with the
  short record (a Delta host on a table): by its size */
  unsigned long legacy=legacy_record(SLOTS_PC);
  CHECK(network_game_layout_from_version(record,legacy,pc,0xFFFF)==NETWORK_GAME_LAYOUT_OK,"a host not known");
  CHECK(widened_ok(SLOTS_PC,0x03),"widened by its size");
  legacy=legacy_record(SLOTS_PC);
  CHECK(network_game_layout_from_version(record,legacy,pc,36)==NETWORK_GAME_LAYOUT_OK,"a host announcing 36");
  CHECK(widened_ok(SLOTS_PC,0x03),"widened by its size");
 } else CASE("console") {
  /* the console's slots (a fold's), the tail where they end */
  unsigned long console=network_game_layout_size(SLOTS_CONSOLE,SLOTS_CONSOLE);
  unsigned long legacy=legacy_record(SLOTS_CONSOLE);
  CHECK(legacy==1920,"an older host's record at the console's slots, %lu",legacy);
  CHECK(network_game_layout_from_version(record,legacy,console,30)==NETWORK_GAME_LAYOUT_OK,"taken");
  CHECK(widened_ok(SLOTS_CONSOLE,0x01),"widened");
 } else CASE("current") {
  /* a host of 36 (or 35, 33): the record as sent */
  unsigned int versions[]={36,35,33,0xFFFF};
  int index;
  for(index=0;index<4;index++){
   memset(record,0,sizeof(record));
   memset(record,0x5A,pc);
   record[NETWORK_GAME_LAYOUT_COOPERATIVE_FLAGS_OFFSET]=0x03;
   memcpy(sent,record,sizeof(record));
   CHECK(network_game_layout_from_version(record,pc,pc,versions[index])==NETWORK_GAME_LAYOUT_OK,"taken");
   CHECK(!memcmp(record,sent,sizeof(record)),"as sent, a host of %u",versions[index]);
  }
 } else CASE("bad_size") {
  unsigned long sizes[]={13135,13121,13119,13137,0};
  int index;
  for(index=0;index<5;index++){
   legacy_record(SLOTS_PC);
   CHECK(network_game_layout_from_version(record,sizes[index],pc,32)==NETWORK_GAME_LAYOUT_BAD_SIZE,
    "a record of %lu bytes",sizes[index]);
   CHECK(!memcmp(record,sent,sizeof(record)),"left as it was");
  }
 } else return 2;
 return 0;
}
