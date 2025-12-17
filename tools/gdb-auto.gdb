set pagination off
set confirm off
set history save off
set logging file gdb-auto.log
set logging overwrite on

define panic-log
    printf "\n===== Forest OS Auto GDB Log =====\n"
    bt full
    info registers
    x/32wx $esp
end

define hook-stop
    set logging on
    panic-log
    set logging off
end

target remote localhost:1234
continue
