#include <fcntl.h>
#include <linux/uinput.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define UINPUT_PATH "/dev/uinput"
#define KEYBOARD_PATH "/dev/input/by-id/usb-Cypress_Cypress_USB_Keyboard___PS2_Mouse-event-kbd"
#define MIN_MSEC 40

static int constructKeyboard (char *name, struct input_id *id, unsigned long *keymask) {
  int i, fd;
  if (-1 == (fd = open(UINPUT_PATH, O_WRONLY))) {
    perror(UINPUT_PATH);
    return -1;
  }

  struct uinput_user_dev description;
  memset(&description, 0, sizeof(description));
  strcpy(description.name, "uinput keyboard");
  description.id.bustype = BUS_USB;
  description.id.vendor = 1;
  description.id.product = 2;
  description.id.version = 3;

  if (write(fd, &description, sizeof(description)) != sizeof(description)) {
    perror("write (struct uinput_user_dev)");
    return -1;
  }

  int key;
  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  ioctl(fd, UI_SET_KEYBIT, KEY_A);
  ioctl(fd, UI_SET_EVBIT, EV_REP);
  ioctl(fd, UI_SET_EVBIT, EV_REL);
  for (key = KEY_RESERVED; key <= KEY_MAX; key++) {
    if ((keymask[key / (sizeof(unsigned long) * 8)] >> (key % (sizeof(unsigned long) * 8))) & 1) {
      ioctl(fd, UI_SET_KEYBIT, key);
    }
  }

  if (ioctl(fd, UI_DEV_CREATE) != -1) {
    return fd;
  }

  perror("ioctl UI_DEV_CREATE");
  return -1;
}

int fd, uinput;

void on_term(int s) {
  ioctl(fd, EVIOCGRAB, 0);
  ioctl(uinput, UI_DEV_DESTROY);
  close(uinput);
  close(fd);
  closelog();
}

int main(int argc, char *argv[]) {
  if (-1 == (fd = open(KEYBOARD_PATH, O_RDONLY))) {
    fprintf(stderr, "Unable to open device %s\n", KEYBOARD_PATH);
    return -1;
  }

  struct input_id id;
  if (-1 == (ioctl(fd, EVIOCGID, &id))) return -1;

  unsigned long keymask[(KEY_MAX+(sizeof(unsigned long)*8)-1)/(sizeof(unsigned long)*8)];
  struct input_event event;

  if (-1 == (ioctl(fd, EVIOCGBIT(EV_KEY, KEY_MAX), keymask))) return -1;

  uinput = constructKeyboard("uinputkeyboard", &id, keymask);
  if (uinput == -1) return -1;

  signal(SIGTERM, on_term);
  openlog("keystroke", LOG_CONS, LOG_USER);

  if (-1 == (ioctl(fd, EVIOCGRAB, 1))) return -1;
  unsigned long time;
  unsigned long press_time[255] = {0};
  while (read(fd, &event, sizeof(event)) == sizeof(event)) {
    if (event.type == EV_KEY) {
      time  = event.time.tv_sec * 1000 + event.time.tv_usec / 1000;
      if (event.value == 1) {
        if (time - press_time[event.code] < MIN_MSEC) {
          syslog(LOG_INFO, "suppress %d %d %d", time, event.value, event.code);
          continue;
        }
      }
      syslog(LOG_INFO, "send %d %d %d", time, event.value, event.code);
      press_time[event.code] = time;
    }
    write(uinput, &event, sizeof(event));
  }

  on_term(0);
  return 0;
}
