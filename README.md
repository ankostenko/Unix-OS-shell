# OS-shell
  Berkeley's OS course. Homework #1.
  Basic shell.
  - help
  - pwd
  - cd 
  - program execution
  - path resolution
  - I/O redirection
  - Signal handling (very limited)
    - We can't kill processes 
    - Processes that works in the background still running even after work of the shell is done
    - We can't resume execution of paused processes (can't send signal CONT)
    - Impossible freely toggle processes between running in the foreground and in the background
    - So basically we can't perfrom any operations that require any sort of keeping information about processes
