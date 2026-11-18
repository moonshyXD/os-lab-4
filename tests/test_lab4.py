import json
import os
import re
from pathlib import Path
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'src'
RESULTS = []


def case(name, commands, expected, success=True, suffix='result.txt'):
    with tempfile.TemporaryDirectory(prefix='lab4-test-') as directory:
        output = Path(directory) / suffix
        run = subprocess.run([str(SRC / 'parent')], cwd=SRC,
                             input=str(output).encode() + b'\n' + commands,
                             capture_output=True, timeout=15)
        assert (run.returncode == 0) == success, (name, run.returncode, run.stderr)
        actual = output.read_text() if output.exists() else None
        assert actual == expected, (name, actual, expected, run.stderr)
    RESULTS.append(name)
    print('PASS:', name, flush=True)


def children(pid):
    path = Path(f'/proc/{pid}/task/{pid}/children')
    return [int(value) for value in path.read_text().split()] if path.exists() else []


def wait_child(parent):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        pids = children(parent.pid)
        if pids:
            return pids[0]
        assert parent.poll() is None, 'parent exited before fork'
        time.sleep(.01)
    raise AssertionError('child did not start')


def process_failures():
    with tempfile.TemporaryDirectory(prefix='lab4-failure-') as directory:
        run = subprocess.run([str(SRC / 'parent')], cwd=directory,
                             input=b'result.txt\n1 2\n', capture_output=True, timeout=5)
        assert run.returncode != 0 and b'execv' in run.stderr
        RESULTS.append('missing child executable')
        for stop in (False, True):
            parent = subprocess.Popen([str(SRC / 'parent')], cwd=SRC,
                                      stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE)
            child = None
            try:
                parent.stdin.write((directory + '/output.txt\n').encode())
                parent.stdin.flush()
                child = wait_child(parent)
                os.kill(child, signal.SIGSTOP if stop else signal.SIGKILL)
                _, err = parent.communicate(b'1 2\n', timeout=5)
                assert parent.returncode != 0, err
                assert not children(parent.pid)
            finally:
                if parent.poll() is None:
                    parent.kill()
                    parent.communicate()
                if child is not None:
                    try:
                        os.kill(child, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
            RESULTS.append('stopped child' if stop else 'killed child')
        parent = subprocess.Popen([str(SRC / 'parent')], cwd=SRC,
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)
        child = None
        try:
            parent.stdin.write((directory + '/orphan.txt\n').encode())
            parent.stdin.flush()
            child = wait_child(parent)
            parent.kill()
            parent.communicate(timeout=5)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                stat = Path(f'/proc/{child}/stat')
                if not stat.exists() or stat.read_text().split()[2] == 'Z':
                    break
                time.sleep(.05)
            else:
                raise AssertionError('child survived parent death')
        finally:
            if parent.poll() is None:
                parent.kill()
                parent.communicate()
            if child is not None:
                try:
                    os.kill(child, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        RESULTS.append('parent death')
    print('PASS: process failure cases', flush=True)


def child_failure_while_reading(stop):
    with tempfile.TemporaryDirectory(prefix='lab4-input-') as directory:
        output = Path(directory) / 'output.txt'
        parent = subprocess.Popen([str(SRC / 'parent')], cwd=SRC,
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)
        child = None
        try:
            # Leave a partial command in the input buffer and keep stdin open.
            parent.stdin.write(f'{output}\n1 2\n4 '.encode())
            parent.stdin.flush()
            child = wait_child(parent)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                wchan = Path(f'/proc/{parent.pid}/wchan').read_text().strip()
                if output.exists() and output.read_text() == '3\n' and 'select' in wchan:
                    break
                time.sleep(.01)
            else:
                raise AssertionError('parent did not wait for the rest of the input')
            os.kill(child, signal.SIGSTOP if stop else signal.SIGKILL)
            assert parent.wait(timeout=5) != 0
            assert not Path(f'/proc/{child}').exists(), 'parent did not reap child'
            assert parent.stdout.read() == f'parent PID={parent.pid}, child PID={child}\n'.encode()
            expected = 'остановлен' if stop else 'завершился во время ввода'
            assert expected.encode() in parent.stderr.read()
        finally:
            if parent.poll() is None:
                parent.kill()
                if child is not None and Path(f'/proc/{child}').exists():
                    os.kill(child, signal.SIGKILL)
            parent.communicate()
    name = 'stopped child during input' if stop else 'killed child during input'
    RESULTS.append(name)
    print('PASS:', name, flush=True)


def trace():
    artifacts = ROOT / 'report' / 'inc'
    trace_path = artifacts / 'ipc-trace.txt'
    with tempfile.TemporaryDirectory(prefix='lab4-trace-') as directory:
        output = Path(directory) / 'result.txt'
        run = subprocess.run(['strace', '-f', '-yy', '-o', str(trace_path),
                              '-e', 'trace=execve,mmap,munmap,ftruncate,kill,rt_sigprocmask,rt_sigtimedwait,pselect6,waitid,wait4,pipe,pipe2,openat,unlink',
                              str(SRC / 'parent')], cwd=SRC,
                             input=f'{output}\n1 2 3\n10 -5 7\n'.encode(),
                             capture_output=True, timeout=15)
        assert run.returncode == 0, run.stderr
        sums = output.read_text()
        assert sums == '6\n12\n'
        assert re.fullmatch(rb'parent PID=[0-9]+, child PID=[0-9]+\n', run.stdout)
        demo = (
            f"$ printf '%s\\n' {output} '1 2 3' '10 -5 7' | ./parent\n"
            + run.stdout.decode() + f'$ cat {output}\n' + sums
        )
        (artifacts / 'demo.txt').write_text(demo)
    text = trace_path.read_text()
    assert 'MAP_SHARED' in text and 'SIGUSR1' in text and 'SIGUSR2' in text
    assert 'ftruncate(' in text and 'pipe(' not in text and 'pipe2(' not in text
    RESULTS.append('strace: mmap/signals, no pipe')
    print('PASS: IPC trace', flush=True)


def main():
    before = set(Path('/tmp').glob('lab4-*'))
    case('positive and negative numbers', b'1 2 3\n10 -5 7\n', '6\n12\n')
    case('int boundaries and wide sum', b'2147483647 2147483647\n-2147483648 -1\n', '4294967294\n-2147483649\n')
    case('one number, no final newline', b'-17', '-17\n')
    case('CRLF and whitespace', b' \t-3\t+5  0\r\n', '2\n')
    case('blank line ends input', b'1 2\n\n100\n', '3\n')
    case('empty input after filename', b'', '')
    case('whitespace-only command', b'  \t\n1\n', '1\n')
    case('invalid token then valid line', b'1 abc 2\n4 5\n', '9\n', False)
    case('integer outside range', b'2147483648\n-2147483649\n3\n', '3\n', False)
    case('numeric suffix', b'12x 3\n', '', False)
    case('NUL in command rejected', b'1\x002\n', '', False)
    case('long command: 100000 integers', b'1 ' * 100000 + b'\n', '100000\n')
    case('1500 commands with changing size', b'1 2 3\n-2\n' * 750, '6\n-2\n' * 750)
    case('missing output directory', b'1 2\n', None, False, 'absent/result.txt')
    run = subprocess.run([str(SRC / 'parent')], cwd=SRC, input=b'/dev/full\n1 2\n', capture_output=True, timeout=5)
    assert run.returncode != 0 and b'write output' in run.stderr
    RESULTS.append('output write failure /dev/full')
    for data in (b'\n', b'bad\x00name\n', b''):
        run = subprocess.run([str(SRC / 'parent')], cwd=SRC, input=data, capture_output=True, timeout=5)
        assert run.returncode != 0
    RESULTS.append('invalid filename inputs')
    process_failures()
    child_failure_while_reading(stop=False)
    child_failure_while_reading(stop=True)
    if not os.environ.get("ASAN_OPTIONS"):
        trace()
    assert set(Path('/tmp').glob('lab4-*')) == before, 'temporary mapping file leaked'
    RESULTS.append('no temporary file leak')
    info = subprocess.run(['gcc', '--version'], capture_output=True, text=True, check=True).stdout.splitlines()[0]
    summary = {'status': 'PASS', 'count': len(RESULTS), 'cases': RESULTS,
               'platform': os.uname().sysname + ' ' + os.uname().release,
               'compiler': info}
    (ROOT / ('report/inc/sanitizer-results.json' if os.environ.get('ASAN_OPTIONS') else 'report/inc/test-results.json')).write_text(json.dumps(summary, ensure_ascii=False, indent=2))
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
