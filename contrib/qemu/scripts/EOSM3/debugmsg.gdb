# ./run_canon_fw.sh EOSM3 -s -S & arm-none-eabi-gdb -x EOSM3/debugmsg.gdb

source -v debug-logging.gdb

macro define CURRENT_TASK 0x803C
macro define CURRENT_ISR  (*(int*)0x8160 ? (*(int*)0x8164) : 0)

b *0xFC37AF70
DebugMsg_log

# DebugMsg0
b *0xFC361A32
commands
  silent
  print_current_location
  printf "[DebugMsg] (%d) %s\n", $r0, $r1
  c
end

b *0x10E1000
assert_log

b *0xBFE14A30
task_create_log

b *0xBFE14998
msleep_log

# semaphores

b *0xBFE15400
take_semaphore_log

b *0xBFE15478
give_semaphore_log

b *0xFC130FE4
commands
  silent
  print_current_location
  KRED
  printf "shutdown!!!\n"
  KRESET
  c
end

cont