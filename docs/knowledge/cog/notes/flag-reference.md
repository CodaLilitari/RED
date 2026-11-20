# Using the replacement flag reference

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/notes/flag-reference.md) (MIT; see [LICENSE](../LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

The maintained [human reference](../flags.md) and [machine catalog](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/reference/flags.json) cover all 26 historical Massassi flag/value families plus five adjacent domains. All 32 ThingFlags bit positions are recorded. Coverage is not a claim that every meaning is proven in retail.

Start with the owning field, exact target and operation. The original JK/MotS wrappers accept nonzero integer masks through OR/AND-complement, without a whitelist; per-bit meanings require separate consumers. Unknown or declaration-only entries are not safe spare state. Enhanced-source and Indy meanings retain their own evidence identities. Source bodies, private captures and original binaries remain local.

Use one family/bit at a time. The JSON is a compact static reference, not a request to crawl, import or rebuild an index. Keep original binary analysis, source analysis and executed behavior distinct when extending a row.
