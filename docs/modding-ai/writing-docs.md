# Writing these guides

How the two modding guides are kept up to date: the player guide in `docs/modding/` and the AI helper guide in `docs/modding-ai/`.
Back to the [front page](README.md).

## When a modding feature lands

Update the guides in the same change:

1. **Page for the feature.** Put the how-to on the page for that area of the player guide (see its front page). If the feature needs a
   new page, add it and list it on the front page. A task page has these parts: what the page covers, steps for each task, a
   complete small example, how to test it, common mistakes, see also.
2. **Front page.** In `docs/modding/README.md`, add or change the line for the page. A feature that exists but is still being tested is
   marked "Experimental" in the status column; a feature that is planned but not built is listed only in `roadmap.md`.
3. **AI helper guide.** Update the compact facts in the matching topic file of `docs/modding-ai/`: `mod.json` fields, data format,
   table names (point to the tool instead of copying lists), Luau hooks (arguments, return values, limits), story format, rules and
   the checklist.
4. **Troubleshooting.** If the feature prints a new message, add it to `docs/modding/troubleshooting.md` with its meaning. Copy the
   text from the source.
5. **Roadmap.** Remove what is now built from `docs/modding/roadmap.md`. Do not add dates.
6. **Examples.** Put new complete examples in `tools/examples`. They must hold no game data.
7. **Tool flags.** Check the flags of every tool in the source before writing a command.

## Writing rules

- Plain words and short sentences. Explain a technical word once, in a line.
- Use real commands that work, and complete examples.
- Do not describe what is not built. Mark a new feature "Experimental" or leave it out.
- Player guide: documentation voice, third person or imperative, no first person, no emoji, no talk about how the page was written.
- No game data: no copied game text, picture, sound or file.
- No names of people, no local paths, no mention of internal work or of unreleased projects. The guides become public.
- Do not copy design notes. Write fresh pages for players.

## Before committing

Run the public export check and make sure both guides pass:

```
python tools/publish/make_public.py --dry-run
```

The guides stay unpublished until the project owner has read and approved them.
