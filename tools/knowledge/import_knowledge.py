#!/usr/bin/env python3
"""Copy the COG and model workshop knowledge into docs/knowledge/.

Usage: python3 -B tools/knowledge/import_knowledge.py <jk-mots-cog-workshop checkout> <JK-MotS-Model-Workflow checkout>

A checkout argument may be `path@commit` for an exported tree.

Run it from RED's repository root. Each checkout must be a clean git clone of the public repository; the
commit it is at is recorded in every copied file. Relative links between copied files are rewritten to
their new locations; links to anything not copied point to the source repository at that commit.
"""
import pathlib
import posixpath
import re
import subprocess
import sys

SOURCES = {
    'cog': {
        'repo': 'CodaLilitari/jk-mots-cog-workshop',
        'licence': 'MIT',
        'files': {
            'docs/GUIDE.md': 'guide.md',
            'docs/COMPATIBILITY.md': 'compatibility.md',
            'docs/FLAGS.md': 'flags.md',
            'docs/FLAG_DISCOVERIES.md': 'flag-discoveries.md',
            'docs/STATIC_CAPACITY.md': 'static-capacity.md',
            'docs/SOURCES.md': 'sources.md',
            'corpus/README.md': 'corpus.md',
            'skill/jk-mots-cog-workflow/SKILL.md': 'workflow.md',
            'NOTICE.md': 'NOTICE.md',
            'LICENSE': 'LICENSE.md',
        },
        # Identical copies of the notes live under skill/.../references/ too.
        'notes': ('corpus/notes', 'skill/jk-mots-cog-workflow/references', 'notes'),
        'aliases': {
            'skill/jk-mots-cog-workflow/references/flags.md': 'docs/FLAGS.md',
            'skill/jk-mots-cog-workflow/references/flag-discoveries.md': 'docs/FLAG_DISCOVERIES.md',
            'skill/jk-mots-cog-workflow/references/static-capacity.md': 'docs/STATIC_CAPACITY.md',
        },
    },
    'models': {
        'repo': 'CodaLilitari/JK-MotS-Model-Workflow',
        'licence': 'MIT',
        'files': {
            'skills/jk-mots-model-workflow/SKILL.md': 'workflow.md',
            'CREDITS.md': 'CREDITS.md',
            'VALIDATION.md': 'VALIDATION.md',
            'LICENSE': 'LICENSE.md',
        },
        'notes': (None, 'skills/jk-mots-model-workflow/references', ''),
        'aliases': {},
        # Workshop maintenance, not modelling knowledge.
        'skip': {'skills/jk-mots-model-workflow/references/updates-and-reporting.md'},
    },
}

SKILL_NOTE = ('> This is the standalone workshop\'s agent skill, kept for its rules and evidence. Its `scripts/` commands '
              'belong to that workshop and are not part of RED. In RED, use `red-agent` and the skills in '
              '[docs/agent_skills](../../agent_skills/README.md).\n\n')

LINK = re.compile(r'(\]\()([^)\s]+)(\))|(href=")([^"]+)(")')


def checkout_and_commit(argument):
    """A git checkout, or `path@commit` for an exported tree whose commit is known."""
    path, at, commit = argument.rpartition('@') if '@' in argument else (argument, '', '')
    checkout = pathlib.Path(path)
    if not commit:
        commit = subprocess.run(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], check=True,
                                capture_output=True, text=True).stdout.strip()
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        sys.exit(f'not a full commit id: {commit}')
    return checkout, commit


def plan(key, checkout):
    """Original path -> copied path, and copied path -> the original it is written from."""
    spec = SOURCES[key]
    mapping = dict(spec['files'])
    primary, secondary, folder = spec['notes']
    if primary:
        for path in sorted((checkout / primary).glob('*.md')):
            mapping[f'{primary}/{path.name}'] = f'{folder}/{path.name}'
    for path in sorted((checkout / secondary).glob('*.md')):
        original = f'{secondary}/{path.name}'
        if original in spec.get('skip', set()):
            continue
        if original in spec['aliases']:
            mapping[original] = mapping[spec['aliases'][original]]
            continue
        if primary:
            twin = checkout / primary / path.name
            if twin.exists() and twin.read_bytes() == path.read_bytes():
                mapping[original] = mapping[f'{primary}/{path.name}']
            continue  # the workshop's own maintenance notes (reporting.md) stay there
        mapping[original] = f'{folder}/{path.name}' if folder else path.name
    canonical = {}
    for original, target in mapping.items():  # files first, then primary notes: insertion order
        canonical.setdefault(target, original)
    return mapping, canonical


def rewrite(text, source, target, mapping, repo, commit):
    def replace(match):
        prefix, link, suffix = (match.group(1), match.group(2), match.group(3)) if match.group(1) else \
            (match.group(4), match.group(5), match.group(6))
        if re.match(r'^[a-z][a-z0-9+.-]*:', link) or link.startswith('#'):
            return prefix + link + suffix
        path, hash_, anchor = link.partition('#')
        resolved = posixpath.normpath(posixpath.join(posixpath.dirname(source), path))
        if resolved.startswith('..'):
            return prefix + link + suffix
        if resolved in mapping:
            new = posixpath.relpath(mapping[resolved], posixpath.dirname(target) or '.')
        else:
            new = f'https://github.com/{repo}/blob/{commit}/{resolved}'
        return prefix + new + hash_ + anchor + suffix
    return LINK.sub(replace, text)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    root = pathlib.Path('docs/knowledge')
    if not pathlib.Path('CMakeLists.txt').exists() or not root.is_dir():
        sys.exit('run from RED\'s repository root, with docs/knowledge/README.md present')
    for key, argument in zip(('cog', 'models'), sys.argv[1:]):
        spec = SOURCES[key]
        checkout, commit = checkout_and_commit(argument)
        mapping, canonical = plan(key, checkout)
        written = set()
        for target, original in sorted(canonical.items()):
            written.add(target)
            text = (checkout / original).read_text(encoding='utf-8')
            if original.endswith('.md'):
                text = rewrite(text, original, target, mapping, spec['repo'], commit)
            url = f'https://github.com/{spec["repo"]}/blob/{commit}/{original}'
            banner = (f'> Copied into RED from [{spec["repo"]}@{commit[:7]}]({url}) ({spec["licence"]}; see '
                      f'[LICENSE]({posixpath.relpath("LICENSE.md", posixpath.dirname(target) or ".")})). '
                      'Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.\n\n')
            if original == 'LICENSE':
                text = '```text\n' + text.rstrip('\n') + '\n```\n'
            if text.startswith('---\n'):  # a skill's front matter; these copies are reference reading
                text = text.split('\n---\n', 1)[1].lstrip('\n')
            if original.endswith('SKILL.md'):
                banner += SKILL_NOTE
            lines = text.split('\n', 1)
            if lines[0].startswith('# '):
                text = lines[0] + '\n\n' + banner + lines[1].lstrip('\n')
            else:
                text = banner + text
            out = root / key / target
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_text(text, encoding='utf-8', newline='\n')
        for old in sorted((root / key).rglob('*')):  # files the source no longer has
            if old.is_file() and old.relative_to(root / key).as_posix() not in written:
                old.unlink()
                print(f'removed {old}')
        print(f'{key}: {len(written)} files from {spec["repo"]}@{commit[:7]}')


if __name__ == '__main__':
    main()
