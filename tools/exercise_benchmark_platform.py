"""独立验证分支的刷新发布和固定确认演练。"""
from pathlib import Path
import json
import os
import sys

VALIDATION_BASELINE_NAME: str = 'benchmark-validation-baseline'
SUSPECT_MULTIPLIER: float = 2.0

if sys.argv[1] == 'policy':
    path: Path = Path('tools/benchmark_policy.json')
    policy: dict = json.loads(path.read_text(encoding='utf-8'))
    policy['baseline']['name'] = VALIDATION_BASELINE_NAME
    policy['baseline']['branch'] = os.environ['GITHUB_REF_NAME']
    path.write_text(json.dumps(policy, indent=2) + '\n', encoding='utf-8')
else:
    group_directory: Path = Path('groups') / sys.argv[2]
    suspects: list[dict] = []
    for path in sorted((group_directory / 'rounds').glob('candidate-*.json')):
        data: dict = json.loads(path.read_text(encoding='utf-8'))
        entry: dict = data['benchmarks'][0]
        suspects.append({'file': path.name, 'name': entry['name'], 'original_cpu_time': entry['cpu_time']})
        entry['cpu_time'] *= SUSPECT_MULTIPLIER
        path.write_text(json.dumps(data) + '\n', encoding='utf-8')
    (group_directory / 'logs' / 'exercise-suspects.json').write_text(
        json.dumps({'multiplier': SUSPECT_MULTIPLIER, 'inputs': suspects}, indent=2), encoding='utf-8',
    )
