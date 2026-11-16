# The news branch

This branch holds only the news that Battlefield 1943 Reborn shows in its news window on the main menu. It is not
code and is never merged into `main` or `in-dev`.

- `news.json`: the feed. The game reads it at every start from
  `https://raw.githubusercontent.com/TekRantGaming/Battlefield-1943-Reborn/news/news.json`.
- `images/`: pictures for the news (JPEG or PNG, 16:9, under 8 MB), used by their raw address.

The format and how to post are in `docs/NEWS.md` on the `in-dev` branch.
