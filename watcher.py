from argparse import ArgumentParser
from subprocess import call
import functools
import logging
import os.path
import platform
import shlex
import signal
import threading
import time

signal.signal(signal.SIGINT, signal.SIG_DFL)

FORMAT = '%(asctime)-15s %(message)s'
logging.basicConfig(format=FORMAT)
logging.getLogger().setLevel(logging.INFO)

SYNC_DELAY = 0.1
BUILD_STATUS_DIR = 'pinboard_build_status'
LOCAL_DIRS_TO_IGNORE = [BUILD_STATUS_DIR]


def _ensure_trailing_slash_present(path_spec):
    path_spec = os.path.normpath(path_spec)

    if path_spec[-1] != os.sep:
        path_spec += os.sep
    return path_spec


class Syncer(threading.Thread):
    """
     Automatically keep two directory structures synchronized. This sync is push
     only, i.e. changes made where the script is run will overwrite the files in
     the destination directory.

     For improved performance, add the following to your ~/.ssh/config file:
     Host <recipient host name>
        ControlPath ~/.ssh/master-%r@%h:%p
        ControlMaster no

     Where <recipient host name> is the DNS name of the computer that receives
     your files. For example, if I'm syncing from localhost to devapp001, the
     recipient host name is devapp001.

    """
    def __init__(self, options):
        super(Syncer, self).__init__()

        self.options = options
        self.source = _ensure_trailing_slash_present(self.options.source)
        self.remote_spec = _ensure_trailing_slash_present(
            self.options.remote_spec)

        self.lock = threading.Lock()
        self.last_event = 0
        self.path = None

    def add_event(self, path):
        with self.lock:
            self.last_event = time.time()
            if self.path is None:
                self.path = path
            else:
                self.path = os.path.commonprefix([self.path, path])

    def set_tab_title(self, title):
        if 'Darwin' in platform.system():
            cmd = 'echo -n "\033]0;%s\007"' % title
            call(shlex.split(cmd))

        logging.info(title)

    def watch_complete(self):
        self.path = None
        self.set_tab_title("Watching...")

    def sync_cb(self):
        with self.lock:
            if not self.path or time.time() - self.last_event < SYNC_DELAY:
                return

            normalized_path = os.path.normpath(self.path)
            if os.path.isdir(normalized_path):
                normalized_path += os.sep

            excludes = ["*.pyc", "*.ldb", ".DS_Store", "build", "node_modules", "*.mo",
                        BUILD_STATUS_DIR, '.dependencies', 'build.log', 'app.log', 'api.log',
                        '*.iml']
            excludes.extend(self.options.extra_excludes)

            if self.options.exclude_git:
                if ".git" in normalized_path:
                    self.watch_complete()
                    return
                excludes.append(".git")

            exclude_cmd = ""

            for exclude in excludes:
                exclude_cmd += " --exclude '%s'" % exclude

            self.set_tab_title('Syncing %s' % self.source)
            cmd = "/usr/bin/rsync -avz --delete {excludes} {source} {remote_spec}".format(
                excludes=exclude_cmd,
                source=self.source,
                remote_spec=self.remote_spec)

            call(shlex.split(cmd))
            self.watch_complete()

    def run(self):
        while True:
            self.sync_cb()
            time.sleep(SYNC_DELAY / 2)


def file_changed(add_event, options, ignored_local_paths, subpath, mask):
    if options.exclude_git and subpath.endswith(".git/"):
        return
    for path in ignored_local_paths:
        if subpath.startswith(path):
            return
    add_event(subpath)


def generate_ignored_local_paths(options):
    source_with_slash = _ensure_trailing_slash_present(options.source)
    ignored_local_paths = []
    for local_dir_to_ignore in LOCAL_DIRS_TO_IGNORE:
        full_local_dir_to_ignore = '%s%s' % (source_with_slash, local_dir_to_ignore)
        ignored_local_paths.append(full_local_dir_to_ignore)
    return ignored_local_paths

parser = ArgumentParser(description="Push local changes to a remote server")
parser.add_argument("-s", dest="source", help="the directory to monitor",
                    required=True)
parser.add_argument("--exclude-git", dest="exclude_git", action="store_true")
parser.add_argument(
    "--exclude", dest="extra_excludes", action="append", default=[])
parser.add_argument("remote_spec")


if __name__ == '__main__':
    options = parser.parse_args()
    ignored_local_paths = generate_ignored_local_paths(options)

    logging.info('Night gathers, and now my watch begins.')

    syncer = Syncer(options)
    syncer.start()
    try:
        import fsevents

        logging.info("Using fsevents.")

        observer = fsevents.Observer()
        stream = fsevents.Stream(
            functools.partial(file_changed, syncer.add_event, options, ignored_local_paths), options.source)
        observer.schedule(stream)
        observer.run()
    except ImportError:
        from watchdog.observers import Observer
        from watchdog.events import FileSystemEventHandler

        logging.info("\033[91m WARNING: Using watchdog!!!\033[0m")
        logging.info("\033[91m Watchdog will consume your CPU and drain your battery. "
                     "To fix, type: sudo pip install macfsevents\033[0m")

        class FileChangedHandler(FileSystemEventHandler):
            """Logs all the events captured."""

            def __init__(self, syncer):
                self.syncer = syncer

            def on_any_event(self, event):
                for ignored_local_path in ignored_local_paths:
                    if event.src_path.startswith(ignored_local_path):
                        return
                super(FileChangedHandler, self).on_any_event(event)
                self.syncer.add_event(event.src_path)

        observer = Observer()
        observer.schedule(
            FileChangedHandler(syncer),
            path=options.source,
            recursive=True
        )
        observer.start()
