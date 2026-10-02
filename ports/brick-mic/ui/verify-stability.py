#!/usr/bin/env python3
"""Check every static page with cloned IPC state and changing focus status."""
import re
from preview import ROOT, run

output = ROOT / 'build/brick-mic/ui-validation'
output.mkdir(parents=True, exist_ok=True)
logs = []
for page in ['main', 'hub', 'edit', 'read', 'approval']:
    commands, report = run('ready', frames=300, page_test=page, focus_churn=True,
                           statistics=True, output=output / ('r14-' + page + '.ppm'))
    match = re.search(r'settled_changed_frames=(\d+) settled_unchanged_frames=(\d+)', report)
    assert match, report
    assert tuple(map(int, match.groups())) == (0, 200), (page, report, commands)
    assert all(command == 'control:tasks' or command.startswith('control:reply:seen:') for command in commands), (page, commands)
    logs.append('PASS: ' + page + '; 200 settled frames unchanged; commands=' + str(commands))
    print(logs[-1], flush=True)
commands = run('ready', frames=160, page_test='hub', input_test=True,
               output=output / 'r14-task-choice.ppm')
assert 'control:choose:00000000-0000-4000-8000-000000000001' in commands, commands
assert not any('alerts' in command for command in commands), commands
logs.append('PASS: task center selection directly opens a task, no reminders submenu')
print(logs[-1])
(output / 'interaction-r14-pages.log').write_text('\n'.join(logs) + '\n')
