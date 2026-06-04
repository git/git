# Instructions for AI Agents

This file gives specific instructions for AI agents that perform
housekeeping tasks for Git l10n. Use of AI is optional; many successful
l10n teams work well without it.

The section "Housekeeping tasks for localization workflows" documents the
most commonly used housekeeping tasks:

1. Generating or updating po/git.pot
2. Updating po/XX.po
3. Translating po/XX.po
4. Reviewing translation quality


## Background knowledge for localization workflows

Essential background for the workflows below; understand these concepts before
performing any housekeeping tasks in this document.

### Language code and notation (XX, ll, ll\_CC)

**XX** is a placeholder for the language code: either `ll` (ISO 639) or
`ll_CC` (e.g. `de`, `zh_CN`). It appears in the PO file header metadata
(e.g. `"Language: zh_CN\n"`) and is typically used to name the PO file:
`po/XX.po`.


### Header Entry

The **header entry** is the first entry in every `po/XX.po`. It has an empty
`msgid`; translation metadata (project, language, plural rules, encoding, etc.)
is stored in `msgstr`, as in this example:

```po
msgid ""
msgstr ""
"Project-Id-Version: Git\n"
"Language: zh_CN\n"
"MIME-Version: 1.0\n"
"Content-Type: text/plain; charset=UTF-8\n"
"Content-Transfer-Encoding: 8bit\n"
"Plural-Forms: nplurals=2; plural=(n != 1);\n"
```

**CRITICAL**: Do not edit the header's `msgstr` while translating. It holds
metadata only and must be left unchanged.


### Glossary Section

PO files may have a glossary in comments before the header entry (first
`msgid ""`), giving terminology guidelines (e.g.):

```po
# Git glossary for Chinese translators
#
#   English                          |  Chinese
#   ---------------------------------+--------------------------------------
#   3-way merge                      |  三路合并
#   ...
```

**IMPORTANT**: Read and use the glossary when translating or reviewing. It is
in `#` comments only. Leave that comment block unchanged.


### PO entry structure (single-line and multi-line)

PO entries are `msgid` / `msgstr` pairs. Plural messages add `msgid_plural` and
`msgstr[n]`. The `msgid` is the immutable source; `msgstr` is the target
translation. Each side may be a single quoted string or a multi-line block.
In the multi-line form the header line is often `msgid ""` / `msgstr ""`, with
the real text split across following quoted lines (concatenated by Gettext).

**Single-line entries**:

```po
msgid "commit message"
msgstr "提交说明"
```

**Multi-line entries**:

```po
msgid ""
"Line 1\n"
"Line 2"
msgstr ""
"行 1\n"
"行 2"
```

**CRITICAL**: Do **not** use `grep '^msgstr ""'` to find untranslated entries;
multi-line `msgstr` blocks use the same opening line, so grep gives false
positives. Use `msgattrib` (next section).


### Locating untranslated, fuzzy, and obsolete entries

Use `msgattrib` to list untranslated, fuzzy, and obsolete entries. Task 3
(translating `po/XX.po`) uses these commands.

- **Untranslated**: `msgattrib --untranslated --no-obsolete po/XX.po`
- **Fuzzy**: `msgattrib --only-fuzzy --no-obsolete po/XX.po`
- **Obsolete** (`#~`): `msgattrib --obsolete --no-wrap po/XX.po`


### Translating fuzzy entries

Fuzzy entries need re-translation because the source text changed. The format
differs by file type:

- **PO file**: A `#, fuzzy` tag in the entry comments marks the entry as fuzzy.
- **JSON file**: The entry has `"fuzzy": true`.

**Translation principles**: Re-translate the `msgstr` (and, for plural entries,
`msgstr[n]`) into the target language. Do **not** modify `msgid` or
`msgid_plural`. After translation, **clear the fuzzy mark**: in PO, remove the
`#, fuzzy` tag from comments; in JSON, omit or set `fuzzy` to `false`.


### Preserving Special Characters

Preserve escape sequences (`\n`, `\"`, `\\`, `\t`), placeholders (`%s`, `%d`,
etc.), and quotes exactly as in `msgid`. Only reorder placeholders with
positional syntax when needed (see Placeholder Reordering below).


### Placeholder Reordering

When reordering placeholders relative to `msgid`, use positional syntax (`%n$`)
where *n* is the 1-based argument index, so each argument still binds to the
right value. Preserve width and precision modifiers, and place `%n$` before
them (see examples below).

**Example 1** (placeholder reordering with precision):

```po
msgid "missing environment variable '%s' for configuration '%.*s'"
msgstr "配置 '%3$.*2$s' 缺少环境变量 '%1$s'"
```

`%s` → argument 1 → `%1$s`. `%.*s` needs precision (arg 2) and string (arg 3) →
`%3$.*2$s`.

**Example 2** (multi-line, four `%s` reordered):

```po
msgid ""
"Path updated: %s renamed to %s in %s, inside a directory that was renamed in "
"%s; moving it to %s."
msgstr ""
"路径已更新：%1$s 在 %3$s 中被重命名为 %2$s，而其所在目录又在 %4$s 中被重命"
"名，因此将其移动到 %5$s。"
```

Original order 1,2,3,4,5; in translation 1,3,2,4,5. Each line must be a
complete quoted string.

**Example 3** (no placeholder reordering):

```po
msgid "MIDX %s must be an ancestor of %s"
msgstr "MIDX %s 必须是 %s 的祖先"
```

Argument order is still 1,2 in translation, so `%n$` is not needed.
If no placeholder reordering occurs, you **must not** introduce `%n$`
syntax; keep the original non-positional placeholders (`%s`, `%d`, etc.).


### Validating PO File Format

Check the PO file using the command below:

```shell
msgfmt --check -o /dev/null po/XX.po
```

Common validation errors include:
- Unclosed quotes
- Missing escape sequences
- Invalid placeholder syntax
- Malformed multi-line entries
- Incorrect line breaks in multi-line strings

On failure, `msgfmt` prints the line number; fix the PO at that line.


### Using git-po-helper

[git-po-helper](https://github.com/git-l10n/git-po-helper) supports Git l10n with
**quality checking** (git-l10n PR conventions) and **AI-assisted translation**
(subcommands for automated workflows). Housekeeping tasks in this document use
it when available; otherwise rely on gettext tools.


#### Splitting large PO files

When a PO file is too large for translation or review, use `git-po-helper
msg-select` to split it by entry index.

- **Entry 0** is the header (included by default; use `--no-header` to omit).
- **Entries 1, 2, 3, …** are content entries.
- **Range format**: `--range "1-50"` (entries 1 through 50), `--range "-50"`
  (first 50 entries), `--range "51-"` (from entry 51 to end). Shortcuts:
  `--head N` (first N), `--tail N` (last N), `--since N` (from N to end).
- **Output format**: PO by default; use `--json` for GETTEXT JSON. See the
  "GETTEXT JSON format" section (under git-po-helper) for details.
- **State filter**: Use `--translated`, `--untranslated`, `--fuzzy` to filter
  by state (OR relationship). Use `--no-obsolete` to exclude obsolete entries;
  `--with-obsolete` to include (default). Use `--only-same` or `--only-obsolete`
  for a single state. Range applies to the filtered list.

```shell
# First 50 entries (header + entries 1–50)
git-po-helper msg-select --range "-50" po/in.po -o po/out.po

# Entries 51–100
git-po-helper msg-select --range "51-100" po/in.po -o po/out.po

# Entries 101 to end
git-po-helper msg-select --range "101-" po/in.po -o po/out.po

# Entries 1–50 without header (content only)
git-po-helper msg-select --range "1-50" --no-header po/in.po -o po/frag.po

# Output as JSON; select untranslated and fuzzy entries, exclude obsolete
git-po-helper msg-select --json --untranslated --fuzzy --no-obsolete po/in.po >po/filtered.json
```


#### Comparing PO files for translation and review

`git-po-helper compare` shows PO changes with full entry context (unlike
`git diff`). Redirect output to a file: it is empty when there are no new or
changed entries; otherwise it contains a valid PO header.

```shell
# Get full context of local changes (HEAD vs working tree)
git-po-helper compare po/XX.po -o po/out.po

# Get full context of changes in a specific commit (parent vs commit)
git-po-helper compare --commit <commit> po/XX.po -o po/out.po

# Get full context of changes since a commit (commit vs working tree)
git-po-helper compare --since <commit> po/XX.po -o po/out.po

# Get full context between two commits
git-po-helper compare -r <commit1>..<commit2> po/XX.po -o po/out.po

# Get full context of two worktree files
git-po-helper compare po/old.po po/new.po -o po/out.po

# Check msgid consistency (detect tampering); no output means target matches source
git-po-helper compare --msgid po/old.po po/new.po >po/out.po
```

**Options summary**

| Option              | Meaning                                        |
|---------------------|------------------------------------------------|
| (none)              | Compare HEAD with working tree (local changes) |
| `--commit <commit>` | Compare parent of commit with the commit       |
| `--since <commit>`  | Compare commit with working tree               |
| `-r x..y`           | Compare revision x with revision y             |
| `-r x..`            | Compare revision x with working tree           |
| `-r x`              | Compare parent of x with x                     |


#### Concatenating multiple PO/JSON files

`git-po-helper msg-cat` merges PO, POT, or gettext JSON inputs into one stream.
Duplicate `msgid` values keep the first occurrence in file order. Write with
`-o <file>` or stdout (`-o -` or omit); `--json` selects JSON output, else PO.

```shell
# Convert JSON to PO (e.g. after translation)
git-po-helper msg-cat --unset-fuzzy -o po/out.po po/in.json

# Merge multiple PO files
git-po-helper msg-cat -o po/out.po po/in-1.po po/in-2.json
```


#### GETTEXT JSON format

The **GETTEXT JSON** format is an internal format defined by `git-po-helper`
for convenient batch processing of translation and related tasks by AI models.
`git-po-helper msg-select`, `git-po-helper msg-cat`, and `git-po-helper compare`
read and write this format.

**Top-level structure**:

```json
{
  "header_comment": "string",
  "header_meta": "string",
  "entries": [ /* array of entry objects */ ]
}
```

| Field            | Description                                                                    |
|------------------|--------------------------------------------------------------------------------|
| `header_comment` | Lines above the first `msgid ""` (comments, glossary), directly concatenated.  |
| `header_meta`    | Encoded `msgstr` of the header entry (Project-Id-Version, Plural-Forms, etc.). |
| `entries`        | List of PO entries. Order matches source.                                      |

**Entry object** (each element of `entries`):

| Field           | Type     | Description                                                  |
|-----------------|----------|--------------------------------------------------------------|
| `msgid`         | string   | Singular message ID. PO escapes encoded (e.g. `\n` → `\\n`). |
| `msgstr`        | []string | Translation forms as a **JSON array only**. Details below.   |
| `msgid_plural`  | string   | Plural form of msgid. Omit for non-plural.                   |
| `comments`      | []string | Comment lines (`#`, `#.`, `#:`, `#,`, etc.).                 |
| `fuzzy`         | bool     | True if entry has fuzzy flag.                                |
| `obsolete`      | bool     | True for `#~` obsolete entries. Omit if false.               |

**`msgstr` array (required shape)**:

- **Always** a JSON array of strings, never a single string. One element = singular
  (PO `msgstr` / `msgstr[0]`); multiple elements = plural forms in order
  (`msgstr[0]`, `msgstr[1]`, …).
- Omit the key or use an empty array when the entry is untranslated.

**Example (single-line entry)**:

```json
{
  "header_comment": "# Glossary:\\n# term1\\tTranslation 1\\n#\\n",
  "header_meta": "Project-Id-Version: git\\nContent-Type: text/plain; charset=UTF-8\\n",
  "entries": [
    {
      "msgid": "Hello",
      "msgstr": ["你好"],
      "comments": ["#. Comment for translator\\n", "#: src/file.c:10\\n"],
      "fuzzy": false
    }
  ]
}
```

**Example (plural entry)**:

```json
{
  "msgid": "One file",
  "msgid_plural": "%d files",
  "msgstr": ["一个文件", "%d 个文件"],
  "comments": ["#, c-format\\n"]
}
```

**Example (fuzzy entry before translation)**:

```json
{
  "msgid": "Old message",
  "msgstr": ["旧翻译。"],
  "comments": ["#, fuzzy\\n"],
  "fuzzy": true
}
```

**Translation notes for GETTEXT JSON files**:

- **Preserve structure**: Keep `header_comment`, `header_meta`, `msgid`,
  `msgid_plural` unchanged.
- **Fuzzy entries**: Entries extracted from fuzzy PO entries have `"fuzzy": true`.
  After translating, **remove the `fuzzy` field** or set it to `false` in the
  output JSON. The merge step uses `--unset-fuzzy`, which can also remove the
  `fuzzy` field.
- **Placeholders**: Preserve `%s`, `%d`, etc. exactly; use `%n$` when
  reordering (see "Placeholder Reordering" above).


### Quality checklist

- **Accuracy**: Faithful to original meaning; no omissions or distortions.
- **Fuzzy entries**: Re-translate fully and clear the fuzzy flag (see
  "Translating fuzzy entries" above).
- **Terminology**: Consistent with glossary (see "Glossary Section" above) or
  domain standards.
- **Grammar and fluency**: Correct and natural in the target language.
- **Placeholders**: Preserve variables (`%s`, `{name}`, `$1`) exactly; use
  positional parameters when reordering (see "Placeholder Reordering" above).
- **Special characters**: Preserve escape sequences (`\n`, `\"`, `\\`, `\t`),
  placeholders exactly as in `msgid`. See "Preserving Special Characters" above.
- **Plurals and gender**: Correct forms and agreement.
- **Context fit**: Suitable for UI space, tone, and use (e.g. error vs. tooltip).
- **Cultural appropriateness**: No offensive or ambiguous content.
- **Consistency**: Match prior translations of the same source.
- **Technical integrity**: Do not translate code, paths, commands, brands, or
  proper nouns.
- **Readability**: Clear, concise, and user-friendly.


## Housekeeping tasks for localization workflows

For common housekeeping tasks, follow the steps in the matching subsection
below.


### Task 1: Generating or updating po/git.pot

When asked to generate or update `po/git.pot` (or the like):

1. **Directly execute** the command `make po/git.pot` without checking
   if the file exists beforehand.

2. **Do not verify** the generated file after execution. Simply run the
   command and consider the task complete.


### Task 2: Updating po/XX.po

When asked to update `po/XX.po` (or the like):

1. **Directly execute** the command `make po-update PO_FILE=po/XX.po`
   without reading or checking the file content beforehand.

2. **Do not verify, translate, or review** the updated file after execution.
   Simply run the command and consider the task complete.


### Task 3: Translating po/XX.po

To translate `po/XX.po`, use the steps below. The script uses gettext or
`git-po-helper` depending on what is installed; JSON export (when available)
supports batch translation rather than per-entry work.

**Workflow loop**: Steps 1→2→3→4→5→6→7 form a loop. After step 6 succeeds,
**always** go to step 7, which returns to step 1. The **only** exit to step 8
is when step 2 finds `po/l10n-pending.po` empty. Do not skip step 7 or jump to
step 8 after step 6.

1. **Extract entries to translate**: **Directly execute** the script below—it is
   authoritative; do not reimplement. It generates `po/l10n-pending.po` with
   messages that need translation.

   ```shell
   l10n_extract_pending () {
       test $# -ge 1 || { echo "Usage: l10n_extract_pending <po-file>" >&2; return 1; }
       PO_FILE="$1"
       PENDING="po/l10n-pending.po"
       PENDING_FUZZY="${PENDING}.fuzzy"
       PENDING_REFER="${PENDING}.fuzzy.reference"
       PENDING_UNTRANS="${PENDING}.untranslated"
       rm -f "$PENDING"

       if command -v git-po-helper >/dev/null 2>&1
       then
           git-po-helper msg-select --untranslated --fuzzy --no-obsolete -o "$PENDING" "$PO_FILE"
       else
           msgattrib --untranslated --no-obsolete "$PO_FILE" >"${PENDING_UNTRANS}"
           msgattrib --only-fuzzy --no-obsolete --clear-fuzzy --empty "$PO_FILE" >"${PENDING_FUZZY}"
           msgattrib --only-fuzzy --no-obsolete "$PO_FILE" >"${PENDING_REFER}"
           msgcat --use-first "${PENDING_UNTRANS}" "${PENDING_FUZZY}" >"$PENDING"
           rm -f "${PENDING_UNTRANS}" "${PENDING_FUZZY}"
       fi
       if test -s "$PENDING"
       then
           msgfmt --stat -o /dev/null "$PENDING" || true
           echo "Pending file is not empty; there are still entries to translate."
       else
           echo "No entries need translation."
           return 1
       fi
   }
   # Run the extraction. Example: l10n_extract_pending po/zh_CN.po
   l10n_extract_pending po/XX.po
   ```

2. **Check generated file**: If `po/l10n-pending.po` is empty or does not exist,
   translation is complete; go to step 8. Otherwise proceed to step 3.

3. **Prepare one batch for translation**: Batching keeps each run small so the
   model can complete translation within limited context. **BEFORE translating**,
   **directly execute** the script below—it is authoritative; do not reimplement.
   Based on which file the script produces: if `po/l10n-todo.json` exists, go to
   step 4a; if `po/l10n-todo.po` exists, go to step 4b.

   ```shell
   l10n_one_batch () {
       test $# -ge 1 || { echo "Usage: l10n_one_batch <po-file> [min_batch_size]" >&2; return 1; }
       PO_FILE="$1"
       min_batch_size=${2:-100}
       PENDING="po/l10n-pending.po"
       TODO_JSON="po/l10n-todo.json"
       TODO_PO="po/l10n-todo.po"
       DONE_JSON="po/l10n-done.json"
       DONE_PO="po/l10n-done.po"
       rm -f "$TODO_JSON" "$TODO_PO" "$DONE_JSON" "$DONE_PO"

       ENTRY_COUNT=$(grep -c '^msgid ' "$PENDING" 2>/dev/null || echo 0)
       ENTRY_COUNT=$((ENTRY_COUNT > 0 ? ENTRY_COUNT - 1 : 0))

       if test "$ENTRY_COUNT" -gt $min_batch_size
       then
           if test "$ENTRY_COUNT" -gt $((min_batch_size * 8))
           then
               NUM=$((min_batch_size * 2))
           elif test "$ENTRY_COUNT" -gt $((min_batch_size * 4))
           then
               NUM=$((min_batch_size + min_batch_size / 2))
           else
               NUM=$min_batch_size
           fi
           BATCHING=1
       else
           NUM=$ENTRY_COUNT
           BATCHING=
       fi

       if command -v git-po-helper >/dev/null 2>&1
       then
           if test -n "$BATCHING"
           then
               git-po-helper msg-select --json --head "$NUM" -o "$TODO_JSON" "$PENDING"
               echo "Processing batch of $NUM entries (out of $ENTRY_COUNT remaining)"
           else
               git-po-helper msg-select --json -o "$TODO_JSON" "$PENDING"
               echo "Processing all $ENTRY_COUNT entries at once"
           fi
       else
           if test -n "$BATCHING"
           then
               awk -v num="$NUM" '/^msgid / && count++ > num {exit} 1' "$PENDING" |
                   tac | awk '/^$/ {found=1} found' | tac >"$TODO_PO"
               echo "Processing batch of $NUM entries (out of $ENTRY_COUNT remaining)"
           else
               cp "$PENDING" "$TODO_PO"
               echo "Processing all $ENTRY_COUNT entries at once"
           fi
       fi
   }
   # Prepare one batch; shrink 2nd arg when batches exceed agent capacity.
   l10n_one_batch po/XX.po 100
   ```

4a. **Translate JSON batch** (`po/l10n-todo.json` → `po/l10n-done.json`):

   - **Task**: Translate `po/l10n-todo.json` (input, GETTEXT JSON) into
     `po/l10n-done.json` (output, GETTEXT JSON). See the "GETTEXT JSON format"
     section above for format details and translation rules.
   - **Reference glossary**: Read the glossary from the batch file's
     `header_comment` (see "Glossary Section" above) and use it for
     consistent terminology.
   - **When translating**: Follow the "Quality checklist" above for correctness
     and quality. Handle escape sequences (`\n`, `\"`, `\\`, `\t`), placeholders,
     and quotes correctly as in `msgid`. For JSON, correctly escape and unescape
     these sequences when reading and writing. Modify `msgstr` and `msgstr[n]`
     (for plural entries); clear the fuzzy flag (omit or set `fuzzy` to `false`).
     Do **not** modify `msgid` or `msgid_plural`.

4b. **Translate PO batch** (`po/l10n-todo.po` → `po/l10n-done.po`):

   - **Task**: Translate `po/l10n-todo.po` (input, GETTEXT PO) into
     `po/l10n-done.po` (output, GETTEXT PO).
   - **Reference glossary**: Read the glossary from the pending file header
     (see "Glossary Section" above) and use it for consistent terminology.
   - **When translating**: Follow the "Quality checklist" above for correctness
     and quality. Preserve escape sequences (`\n`, `\"`, `\\`, `\t`), placeholders,
     and quotes as in `msgid`. Modify `msgstr` and `msgstr[n]` (for plural
     entries); remove the `#, fuzzy` tag from comments when done. Do **not**
     modify `msgid` or `msgid_plural`.

5. **Validate `po/l10n-done.po`**:

   Run the validation script below. If it fails, fix per the errors and notes,
   re-run until it succeeds.

   ```shell
   l10n_validate_done () {
       DONE_PO="po/l10n-done.po"
       DONE_JSON="po/l10n-done.json"
       PENDING="po/l10n-pending.po"

       if test -f "$DONE_JSON" && { ! test -f "$DONE_PO" || test "$DONE_JSON" -nt "$DONE_PO"; }
       then
           git-po-helper msg-cat --unset-fuzzy -o "$DONE_PO" "$DONE_JSON" || {
               echo "ERROR [JSON to PO conversion]: Fix $DONE_JSON and re-run." >&2
               return 1
           }
       fi

       # Check 1: msgid should not be modified
       MSGID_OUT=$(git-po-helper compare -q --msgid --assert-no-changes \
           "$PENDING" "$DONE_PO" 2>&1)
       MSGID_RC=$?
       if test $MSGID_RC -ne 0 || test -n "$MSGID_OUT"
       then
           echo "ERROR [msgid modified]: The following entries appeared after" >&2
           echo "translation because msgid was altered. Fix in $DONE_PO." >&2
           echo "$MSGID_OUT" >&2
           return 1
       fi

       # Check 2: PO format (see "Validating PO File Format" for error handling)
       MSGFMT_OUT=$(msgfmt --check -o /dev/null "$DONE_PO" 2>&1)
       MSGFMT_RC=$?
       if test $MSGFMT_RC -ne 0
       then
           echo "ERROR [PO format]: Fix errors in $DONE_PO." >&2
           echo "$MSGFMT_OUT" >&2
           return 1
       fi

       echo "Validation passed."
   }
   l10n_validate_done
   ```

   If the script fails, fix **directly in `po/l10n-done.po`**. Re-run
   `l10n_validate_done` until it succeeds. Editing `po/l10n-done.json` is not
   recommended because it adds an extra JSON-to-PO conversion step. Use the
   error message to decide:

   - **`[msgid modified]`**: The listed entries have altered `msgid`; restore
     them to match `po/l10n-pending.po`.
   - **`[PO format]`**: `msgfmt` reports line numbers; fix the errors in place.
     See "Validating PO File Format" for common issues.


6. **Merge translation results into `po/XX.po`**: Run the script below. If it
   fails, fix the file the error names: **`[JSON to PO conversion]`** →
   `po/l10n-done.json`; **`[msgcat merge]`** → `po/l10n-done.po`. Re-run until
   it succeeds.

   ```shell
   l10n_merge_batch () {
       test $# -ge 1 || { echo "Usage: l10n_merge_batch <po-file>" >&2; return 1; }
       PO_FILE="$1"
       DONE_PO="po/l10n-done.po"
       DONE_JSON="po/l10n-done.json"
       MERGED="po/l10n-done.merged"
       PENDING="po/l10n-pending.po"
       PENDING_REFER="${PENDING}.fuzzy.reference"
       TODO_JSON="po/l10n-todo.json"
       TODO_PO="po/l10n-todo.po"
       if test -f "$DONE_JSON" && { ! test -f "$DONE_PO" || test "$DONE_JSON" -nt "$DONE_PO"; }
       then
           git-po-helper msg-cat --unset-fuzzy -o "$DONE_PO" "$DONE_JSON" || {
               echo "ERROR [JSON to PO conversion]: Fix $DONE_JSON and re-run." >&2
               return 1
           }
       fi
       msgcat --use-first "$DONE_PO" "$PO_FILE" >"$MERGED" || {
           echo "ERROR [msgcat merge]: Fix errors in $DONE_PO and re-run." >&2
           return 1
       }
       mv "$MERGED" "$PO_FILE"
       rm -f "$TODO_JSON" "$TODO_PO" "$DONE_JSON" "$DONE_PO" "$PENDING_REFER"
   }
   # Run the merge. Example: l10n_merge_batch po/zh_CN.po
   l10n_merge_batch po/XX.po
   ```

7. **Loop**: **MUST** return to step 1 (Extract entries) and repeat the cycle.
   Do **not** skip this step or go to step 8. Step 8 (below) runs **only**
   when step 2 finds no more entries and redirects there.

8. **Only after loop exits**: Run the command below to validate the PO file and
   display the report. The process ends here.

   ```shell
   msgfmt --check --stat -o /dev/null po/XX.po
   ```


### Task 4: Review translation quality

Review may target the full `po/XX.po`, a specific commit, or changes since a
commit. When asked to review, follow the steps below.

**Workflow**: Follow steps in order. Do **NOT** use `git show`, `git diff`,
`git format-patch`, or similar to get changes—they break PO context; use **only**
`git-po-helper compare` for extraction. Without `git-po-helper`, refuse the task.
Steps 3→4→5→6→7 loop: after step 6, **always** go to step 7 (back to step 3).
The **only** ways to step 8 are when step 4 finds `po/review-todo.json` missing
or empty (no batch left to review), or when step 1 finds `po/review-result.json`
already present.

1. **Check for existing review (resume support)**: Evaluate the following in order:

   - If `po/review-input.po` does **not** exist, proceed to step 2 (Extract
     entries) for a fresh start.
   - Else If `po/review-result.json` exists, go to step 8 (only after loop exits).
   - Else If `po/review-done.json` exists, go to step 6 (Rename result).
   - Else if `po/review-todo.json` exists, go to step 5 (Review the current
     batch).
   - Else go to step 3 (Prepare one batch).

2. **Extract entries**: Run `git-po-helper compare` with the desired range and
   redirect the output to `po/review-input.po`. See "Comparing PO files for
   translation and review" under git-po-helper for options.

3. **Prepare one batch**: Batching keeps each run small so the model can
   complete review within limited context. **Directly execute** the script
   below—it is authoritative; do not reimplement.

   ```shell
   review_one_batch () {
       min_batch_size=${1:-100}
       INPUT_PO="po/review-input.po"
       PENDING="po/review-pending.po"
       TODO="po/review-todo.json"
       DONE="po/review-done.json"
       BATCH_FILE="po/review-batch.txt"

       if test ! -f "$INPUT_PO"
       then
           rm -f "$TODO"
           echo >&2 "cannot find $INPUT_PO, nothing for review"
           return 1
       fi
       if test ! -f "$PENDING" || test "$INPUT_PO" -nt "$PENDING"
       then
           rm -f "$BATCH_FILE" "$TODO" "$DONE"
           rm -f po/review-result*.json
           cp "$INPUT_PO" "$PENDING"
       fi

       ENTRY_COUNT=$(grep -c '^msgid ' "$PENDING" 2>/dev/null || echo 0)
       ENTRY_COUNT=$((ENTRY_COUNT > 0 ? ENTRY_COUNT - 1 : 0))
       if test "$ENTRY_COUNT" -eq 0
       then
           rm -f "$TODO"
           echo >&2 "No entries left for review"
           return 1
       fi

       if test "$ENTRY_COUNT" -gt $min_batch_size
       then
           if test "$ENTRY_COUNT" -gt $((min_batch_size * 8))
           then
               NUM=$((min_batch_size * 2))
           elif test "$ENTRY_COUNT" -gt $((min_batch_size * 4))
           then
               NUM=$((min_batch_size + min_batch_size / 2))
           else
               NUM=$min_batch_size
           fi
       else
           NUM=$ENTRY_COUNT
       fi

       BATCH=$(cat "$BATCH_FILE" 2>/dev/null || echo 0)
       BATCH=$((BATCH + 1))
       echo "$BATCH" >"$BATCH_FILE"

       git-po-helper msg-select --json --head "$NUM" -o "$TODO" "$PENDING"
       git-po-helper msg-select --since "$((NUM + 1))" -o "${PENDING}.tmp" "$PENDING"
       mv "${PENDING}.tmp" "$PENDING"
       echo "Processing batch $BATCH ($NUM entries out of $ENTRY_COUNT)"
   }
   # The parameter controls batch size; reduce if the batch file is too large.
   review_one_batch 100
   ```

4. **Check todo file**: If `po/review-todo.json` does not exist or is empty,
   review is complete; go to step 8 (only after loop exits). Otherwise proceed to
   step 5.

5. **Review the current batch**: Review translations in `po/review-todo.json`
   and write findings to `po/review-done.json` as follows:
   - Use "Background knowledge for localization workflows" for PO/JSON structure,
     placeholders, and terminology.
   - If `header_comment` includes a glossary, follow it for consistency.
   - Do **not** review the header (`header_comment`, `header_meta`).
   - For every other entry, check the entry's `msgstr` **array** (translation
     forms) against `msgid` / `msgid_plural` using the "Quality checklist" above.
   - Write JSON per "Review result JSON format" below; use `{"issues": []}` when
     there are no issues. **Always** write `po/review-done.json`—it marks the
     batch complete.

6. **Rename result**: Rename `po/review-done.json` to `po/review-result-<N>.json`,
   where N is the value in `po/review-batch.txt` (the batch just completed).
   Run the script below:

   ```shell
   review_rename_result () {
       TODO="po/review-todo.json"
       DONE="po/review-done.json"
       BATCH_FILE="po/review-batch.txt"
       if test -f "$DONE"
       then
           N=$(cat "$BATCH_FILE" 2>/dev/null) || { echo "ERROR: $BATCH_FILE not found." >&2; return 1; }
           mv "$DONE" "po/review-result-$N.json"
           echo "Renamed to po/review-result-$N.json"
       fi
       rm -f "$TODO"
   }
   review_rename_result
   ```

7. **Loop**: **MUST** return to step 3 (Prepare one batch) and repeat the cycle.
   Do **not** skip this step or go to step 8. Step 8 is reached **only** when
   step 4 finds `po/review-todo.json` missing or empty.

8. **Only after loop exits**: **Directly execute** the command below. It merges
   results, applies suggestions, and displays the report. The process ends here.

   ```shell
   git-po-helper agent-run review --report po
   ```

   **Do not** run cleanup or delete intermediate files. Keep them for inspection
   or resumption.

**Review result JSON format**:

The **Review result JSON** format defines the structure for translation
review reports. For each entry with translation issues, create an issue
object as follows:

- Copy the original entry's `msgid`, optional `msgid_plural`, and optional
  `msgstr` array (original translation forms) into the issue object. Use the
  same shape as GETTEXT JSON: `msgstr` is **always a JSON array** when present
  (one element singular, multiple for plural).
- Write a summary of all issues found for this entry in `description`.
- Set `score` according to the severity of issues found for this entry,
  from 0 to 3 (0 = critical; 1 = major; 2 = minor; 3 = perfect, no issues).
  **Lower score means more severe issues.**
- Place the suggested translation in **`suggest_msgstr`** as a **JSON array**:
  one string for singular, multiple strings for plural forms in order. This is
  required for `git-po-helper` to apply suggestions.
- Include only entries with issues (score less than 3). When no issues are
  found in the batch, write `{"issues": []}`.

Example review result (with issues):

```json
{
  "issues": [
    {
      "msgid": "commit",
      "msgstr": ["委托"],
      "score": 0,
      "description": "Terminology error: 'commit' should be translated as '提交'",
      "suggest_msgstr": ["提交"]
    },
    {
      "msgid": "repository",
      "msgid_plural": "repositories",
      "msgstr": ["版本库", "版本库"],
      "score": 2,
      "description": "Consistency issue: suggest using '仓库' consistently",
      "suggest_msgstr": ["仓库", "仓库"]
    }
  ]
}
```

Field descriptions for each issue object (element of the `issues` array):

- `msgid` (and optional `msgid_plural` for plural entries): Original source text.
- `msgstr` (optional): JSON array of original translation forms (same meaning as
  in GETTEXT JSON entries).
- `suggest_msgstr`: JSON array of suggested translation forms; **must be an
  array** (e.g. `["提交"]` for singular). Plural entries use multiple elements
  in order.
- `score`: 0–3 (0 = critical; 1 = major; 2 = minor; 3 = perfect, no issues).
- `description`: Brief summary of the issue.


## Human translators remain in control

Git translation is human-driven; language team leaders and contributors are
responsible for maintaining translation quality and consistency.

AI-generated output should always be treated as drafts that must be reviewed
and approved by someone who understands both the technical context and the
target language. The best results come from combining AI efficiency with human
judgment, cultural insight, and community engagement.
