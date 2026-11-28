The ten-minute Mac test (for someone with an Apple Silicon Mac: M1, M2, M3, M4)
===============================================================================

What this is, in plain words
----------------------------
We want to make Pokemon XD run as a "real" Apple Silicon program instead of through Rosetta (Apple is removing
Rosetta in autumn 2027). Before anyone writes that version, three small questions about how macOS treats a program's
memory have to be answered, and only a real Apple Silicon Mac can answer them. This test is a tiny program (one
file, about 300 lines of C, no game, no disc, nothing is installed, nothing is sent anywhere) that asks macOS the three
questions and writes the answers into one text file.

It takes about ten minutes the first time and about ten seconds when it runs.

What you need
-------------
- A Mac with an Apple chip (not an Intel Mac).
- Apple's command line tools (the compiler). If you do not have them, the Terminal offers to install them the first time
  you type "clang"; say yes (a few minutes, free, no Apple ID needed).
- The folder this file is in (tools/mac/window_probe).

Steps
-----
1. Open the Terminal (Applications > Utilities > Terminal).
2. Type  cd  and a space, then drag the folder "window_probe" from Finder into the Terminal window, then press Return.
3. Type this and press Return:

       sh run_probe.sh

4. Wait a few seconds. It prints a short list ending with lines that start with FACT. Whatever it prints, it is fine:
   a "NO" is a useful answer, not a failure.
5. Send back the file  out/probe_report.txt  (it is inside the window_probe folder). That is all.

If it stops with an error before printing anything (for example "clang: command not found"), send the error text.
If macOS says a program "cannot be opened because the developer cannot be verified", it did not come from this folder:
the script builds its own programs here, so this should not happen; if it does, tell us the exact words.

What the three questions are (so you know what it does)
-------------------------------------------------------
FACT 1: Can the program reserve one big block of 4 GB of address space (not real memory: just addresses, nothing is
        used until later), turn pieces of it into usable memory, and show the same memory at two addresses inside it?
        (The game's main memory has to be visible twice.)
FACT 2: When the program asks for that block at a high address of its own choosing (128 GB and up), does macOS give
        it exactly there? (The plan puts the game's memory block there.)
FACT 3: Does the compiler's linker accept a program file that holds a huge empty segment (0.5 GB and 3 GB), and does
        macOS start such a program? Three small variants (window_probe_seg512, window_probe_seg3g,
        window_probe_segfix) are built for this; if one cannot be built, the report says so, and that is an answer.

Nothing is written outside the folder "out", the programs allocate address space only (a few megabytes of real
memory), and they end by themselves.

Without a Mac of your own
-------------------------
The project's GitHub page has a workflow "Mac build". Choose "Run workflow", stage "probe": GitHub's own Apple Silicon
machine (macos-15) does exactly the steps above and keeps probe_report.txt as a download called
"window-probe-report" on the run's page. Nothing else of the workflow runs in that mode.

For the person who reads the answers (not needed to run it)
-----------------------------------------------------------
docs/MAC_ARM.md, "The probe": what each answer decides (FACT 1 no = the whole plan is dead; FACT 2 no = the window is
put where the kernel puts it, which is also acceptable; FACT 3 decides whether the game's reserved ranges can stay in
the program file or must be made at run time).
