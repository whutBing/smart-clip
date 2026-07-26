# SmartClip Translation Guide

Welcome, and thank you for helping translate SmartClip! This guide explains how to add a new language or improve an existing translation.

## How to Add a New Language

1. **Fork the repository** on GitHub and clone your fork locally.
2. **Copy the reference file**: duplicate `lang/en-US.ini` and rename the copy to `lang/<your-language-code>.ini` (for example, `lang/fr-FR.ini` for French, `lang/pt-BR.ini` for Brazilian Portuguese).
3. **Translate all string values** in the `[strings]` section. Translate only the value to the right of the `=` sign — never change the key on the left.
4. **Update the `[info]` section** at the top of your file:
   - `name` — the language name written **in its native form** (e.g. `Français`, `日本語`, `العربية`).
   - `code` — the language code that matches your filename (e.g. `fr-FR`).
   - `rtl` — `true` for right-to-left languages such as Arabic or Hebrew, `false` otherwise.
5. **Test your file** by loading it in SmartClip (place it in the `lang/` directory and select the language in Settings → Language).
6. **Submit a Pull Request** on GitHub with a clear title such as `Add French (fr-FR) translation`.

## File Format

- **Encoding**: UTF-8 **without BOM**.
- **Structure**: standard INI format with two sections — `[info]` and `[strings]`.
- **Comments**: lines beginning with `#` or `;` are treated as comments and ignored.
- **Key-value pairs**: write as `KEY=value`. Do not quote values unless the quotes are part of the string itself.
- **Escape sequences** (use these literally in values — do not replace them with real line breaks in the file):
  - `\r\n` — Windows-style line break (used in EULA and privacy policy bodies)
  - `\n` — newline (used in shorter multi-line dialogs)
  - `\t` — tab
  - `\\` — literal backslash

## Translation Guidelines

- **Keep the brand name**: leave `Smart Clip` / `SmartClip` untranslated in `STR_APP_TITLE` and similar brand references.
- **Keyboard modifiers are unchanged**: `Ctrl`, `Alt`, `Shift`, `Win` should remain in their original Latin form across all languages (these match physical key labels).
- **Language name keys**: every translation file must list all `STR_LANGUAGE_*` keys. Translate each one to **its own native name** — for example, `STR_LANGUAGE_ZH_CN=简体中文` should appear with that exact value in every file, including English.
- **Preserve escape sequences**: in long text fields such as `STR_EULA_BODY` and `STR_PRIVACY_BODY`, keep every `\r\n` exactly where it appears in the English reference. Do not convert them to real newlines and do not remove them.
- **Legal texts must be accurate**: the EULA (`STR_EULA_*`) and Privacy Policy (`STR_PRIVACY_*`) are legal documents. Translate them carefully and faithfully — do not paraphrase, summarize, or omit clauses. If you are unsure about a legal term, please ask in a GitHub issue before submitting.
- **Placeholders**: keep format placeholders such as `%s` intact and in the same logical position within the translated sentence.
- **Tone**: use a clear, neutral, user-friendly tone consistent with desktop software in your language. Be consistent with terminology across strings.
- **Length**: try to keep translations reasonably close in length to the English source so they fit the UI. If a string is significantly longer, consider a more concise wording.

## Currently Supported Languages

| Language   | Code   | Native Name | RTL   | Status      |
|------------|--------|-------------|-------|-------------|
| English    | en-US  | English     | false | Reference   |
| Chinese    | zh-CN  | 简体中文     | false | Available   |
| Japanese   | ja-JP  | 日本語       | false | Available   |
| Korean     | ko-KR  | 한국어       | false | Available   |
| German     | de-DE  | Deutsch     | false | Available   |
| Arabic     | ar-SA  | العربية     | true  | Available   |
| Turkish    | tr-TR  | Türkçe      | false | Available   |

If your language is not listed above, that means it has not been contributed yet — we'd love for you to be the first!

## Language Code Reference

Language codes follow the `language-COUNTRY` pattern from [BCP 47](https://tools.ietf.org/html/bcp47). Some common examples:

| Code   | Language               |
|--------|------------------------|
| en-US  | English (United States)|
| zh-CN  | Chinese (Simplified)   |
| ja-JP  | Japanese               |
| ko-KR  | Korean                 |
| de-DE  | German                 |
| ar-SA  | Arabic (Saudi Arabia)  |
| tr-TR  | Turkish                |
| fr-FR  | French                 |
| es-ES  | Spanish (Spain)        |
| es-419 | Spanish (Latin America)|
| pt-BR  | Portuguese (Brazil)    |
| pt-PT  | Portuguese (Portugal)  |
| ru-RU  | Russian                |
| it-IT  | Italian                |
| nl-NL  | Dutch                  |
| pl-PL  | Polish                 |
| hi-IN  | Hindi                  |
| id-ID  | Indonesian             |
| th-TH  | Thai                   |
| vi-VN  | Vietnamese             |
| he-IL  | Hebrew                 |
| fa-IR  | Persian                |
| uk-UA  | Ukrainian              |

Choose the code that best matches the variant you are translating. When in doubt, open an issue and ask.

## Questions?

If you have any questions about translating SmartClip — terminology, ambiguity in a source string, RTL handling, or anything else — please [open an issue on GitHub](https://github.com/smartclip/smartclip/issues) and we'll be happy to help.

Happy translating! 🌍
