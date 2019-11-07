/*
 * xfer_reset.c
 */

#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>

#include <libusb-1.0/libusb.h>


#define USB_VID		0x0525
#define USB_PID		0xa4a0

#define EP_DATA_OUT	(1 | LIBUSB_ENDPOINT_OUT)

/* tweakable parameters */
#define NUM_REQS	10	/* number of simultaneous URBs queued */
#define XFER_SIZE	4096	/* data transfer size per URB */
#define TIME_BEFORE_RESET_US 500000 /* time to wait for sending RESET after transfer starts, in uS */

static int do_exit = 0;
static struct libusb_device_handle *devh = NULL;

static unsigned long num_bytes = 0, num_xfer = 0;

struct my_xfer {
	struct libusb_transfer *xfer;
	int id;
};

static struct my_xfer xfers[NUM_REQS];
static pthread_t reset_thread;

static void LIBUSB_CALL cb_xfer(struct libusb_transfer *xfer)
{
	int i;

	if (xfer->status != LIBUSB_TRANSFER_COMPLETED) {
		fprintf(stderr, "%d: transfer status %d\n", *(int *)xfer->user_data, xfer->status);
		libusb_free_transfer(xfer);
		do_exit = 1;
	}

	num_bytes += xfer->actual_length;
	num_xfer++;

	if (do_exit)
		return;

	if (libusb_submit_transfer(xfer) < 0) {
		fprintf(stderr, "%d: error re-submitting URB\n", *(int *)xfer->user_data);
		libusb_free_transfer(xfer);
		return;
	}
}

int start_transfer()
{
	static int buf[XFER_SIZE];
	int i;

	for (i = 0; i < NUM_REQS; i++) {
		xfers[i].id = i;
		xfers[i].xfer  = libusb_alloc_transfer(0);
		if (!xfers[i].xfer)
			return -ENOMEM;

		libusb_fill_bulk_transfer(xfers[i].xfer, devh, EP_DATA_OUT, (char *)buf,
				XFER_SIZE, cb_xfer, &xfers[i].id, NUM_REQS);
	}

	for (i = 0; i < NUM_REQS; i++)
		libusb_submit_transfer(xfers[i].xfer);

	return 0;
}

void cancel_transfers()
{
	int i;

	for (i = 0; i < NUM_REQS; i++)
		libusb_cancel_transfer(xfers[i].xfer);
}

static void *reset_thread_fn(void *arg)
{
        int r = 0;
        printf("reset thread running\n");

        (void)arg;

	usleep(TIME_BEFORE_RESET_US);
	if (!do_exit) {
		printf("send reset\n");
		printf("%lu transfers (total %lu bytes)\n",
			num_xfer, num_bytes);
		libusb_reset_device(devh);
		num_xfer = 0;
		num_bytes = 0;
        }

        printf("reset thread shutting down\n");
        return NULL;
}

static void sig_hdlr(int signum)
{
	switch (signum) {
	case SIGINT:
	case SIGTERM:
	case SIGQUIT:
		do_exit = 1;
		break;
	}
}

int main(int argc, char **argv)
{
	int rc;
	struct sigaction sigact;

	sigact.sa_handler = sig_hdlr;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
        sigaction(SIGTERM, &sigact, NULL);
        sigaction(SIGQUIT, &sigact, NULL);

	rc = libusb_init(NULL);
	if (rc < 0) {
		fprintf(stderr, "Error initializing libusb: %s\n", libusb_error_name(rc));
		exit(1);
	}

	devh = libusb_open_device_with_vid_pid(NULL, 0x0525, 0xa4a0);
	if (!devh) {
		fprintf(stderr, "Error finding USB device\n");
		goto out;
	}

	rc = libusb_detach_kernel_driver(devh, 0);
	if (rc && rc != LIBUSB_ERROR_NOT_FOUND) {
		fprintf(stderr, "Error detaching kernel driver: %s\n", libusb_error_name(rc));
		goto out;
	}

	rc = libusb_claim_interface(devh, 0);
	if (rc < 0) {
		fprintf(stderr, "Error claiming interface: %s\n", libusb_error_name(rc));
		goto out;
	}

	rc = start_transfer();
	if (rc) {
		fprintf(stderr, "Error initiating transfer: %s\n", libusb_error_name(rc));
		goto release;
	}

	rc = pthread_create(&reset_thread, NULL, reset_thread_fn, NULL);
	if (rc) {
		fprintf(stderr, "Error creating reset thread: %s\n", libusb_error_name(rc));
		cancel_transfers();
		rc = 0;
	}

	/* wait till user interrupts or transfer error */
	while (!do_exit) {
		rc = libusb_handle_events(NULL);
		if (rc != LIBUSB_SUCCESS)
			break;
	}

        printf("shutting down...\n");
        pthread_join(reset_thread, NULL);

release:
	libusb_release_interface(devh, 0);
out:
	if (devh)
		libusb_close(devh);
	libusb_exit(NULL);
	return rc;
}
