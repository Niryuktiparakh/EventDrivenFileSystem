from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler
from datetime import datetime

from engine.events import FileEvent


class WatcherHandler(FileSystemEventHandler):

    def on_created(self, event):

        if event.is_directory:
            return

        file_event = FileEvent(
            event_type="CREATE",
            path=event.src_path,
            timestamp=datetime.now()
        )

        print_event(file_event)

    def on_modified(self, event):

        if event.is_directory:
            return

        file_event = FileEvent(
            event_type="MODIFY",
            path=event.src_path,
            timestamp=datetime.now()
        )

        print_event(file_event)

    def on_deleted(self, event):

        if event.is_directory:
            return

        file_event = FileEvent(
            event_type="DELETE",
            path=event.src_path,
            timestamp=datetime.now()
        )

        print_event(file_event)

    def on_moved(self, event):

        if event.is_directory:
            return

        file_event = FileEvent(
            event_type="RENAME",
            path=event.dest_path,
            timestamp=datetime.now(),
            old_path=event.src_path
        )

        print_event(file_event)


def print_event(file_event):

    print("\n[EVENT]", file_event.event_type)

    if file_event.event_type == "RENAME":

        print("Old:", file_event.old_path)
        print("New:", file_event.path)

    else:

        print("File:", file_event.path)


def start_watcher(folder):

    observer = Observer()

    handler = WatcherHandler()

    observer.schedule(
        handler,
        folder,
        recursive=True
    )

    observer.start()

    print("[ENGINE] Monitoring started...")
    print("[ENGINE] Watching:", folder)

    try:

        while True:
            pass

    except KeyboardInterrupt:

        print("\n[ENGINE] Stopping...")

        observer.stop()

    observer.join()