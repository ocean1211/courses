/******************************************************************************
 * ctcp.c
 * ------
 * Implementation of cTCP done here. This is the only file you need to change.
 * Look at the following files for references and useful functions:
 *   - ctcp.h: Headers for this file.
 *   - ctcp_iinked_list.h: Linked list functions for managing a linked list.
 *   - ctcp_sys.h: Connection-related structs and functions, cTCP segment
 *                 definition.
 *   - ctcp_utils.h: Checksum computation, getting the current time.
 *
 *****************************************************************************/

#include "ctcp.h"
#include "ctcp_linked_list.h"
#include "ctcp_sys.h"
#include "ctcp_utils.h"

#define CTCP_HDR_SIZE sizeof(ctcp_segment_t)

/**
 * Connection state.
 *
 * Stores per-connection information such as the current sequence number,
 * unacknowledged packets, etc.
 *
 * You should add to this to store other fields you might need.
 */
struct ctcp_state {
  struct ctcp_state *next;  /* Next in linked list */
  struct ctcp_state **prev; /* Prev in linked list */

  conn_t *conn;             /* Connection object -- needed in order to figure
                               out destination when sending */
  linked_list_t *segments;  /* Linked list of segments sent to this connection.
                               It may be useful to have multiple linked lists
                               for unacknowledged segments, segments that
                               haven't been sent, etc. Lab 1 uses the
                               stop-and-wait protocol and therefore does not
                               necessarily need a linked list. You may remove
                               this if this is the case for you */

  /* FIXME: Add other needed fields. */
  uint32_t seqno;   // Current sequence number
  uint32_t ackno;   // Current acknowlegement number
  ctcp_segment_t *sent_segment;
  int sent_segment_len;
  ctcp_segment_t *recv_segment;
  int recv_segment_len;
};

/**
 * Linked list of connection states. Go through this in ctcp_timer() to
 * resubmit segments and tear down connections.
 */
static ctcp_state_t *state_list;

/* FIXME: Feel free to add as many helper functions as needed. Don't repeat
          code! Helper functions make the code clearer and cleaner. */

/*
 * returns: The number of bytes actually sent, 0 if nothing was sent, or -1 if
 *          there was an error.
 */
int send_ctcp(ctcp_state_t *state, uint32_t flags, char *data, int len) {
    int segment_len = CTCP_HDR_SIZE + len;
    ctcp_segment_t *segment = malloc(segment_len);
    segment->seqno = htonl(state->seqno);
    segment->ackno = htonl(state->ackno);
    segment->len = htons(segment_len);
    segment->flags = htonl(flags);
    segment->window = htons(MAX_SEG_DATA_SIZE);
    segment->cksum = 0;
    if (len > 0) {
        memcpy(segment->data, data, len);
    }
    segment->cksum = cksum(segment, segment_len);

    // A purge ACK packet
    if (flags == ACK && len == 0) {
        return conn_send(state->conn, segment, segment_len);
    }

    state->sent_segment_len = segment_len;
    state->sent_segment = segment;
    return conn_send(state->conn, segment, segment_len);
}

int valid_cksum(ctcp_segment_t *segment, int len) {
    uint16_t old_cksum = segment->cksum;
    segment->cksum = 0;
    uint16_t correct_cksum = cksum(segment, len);
    segment->cksum = old_cksum;
    return old_cksum == correct_cksum;
}


ctcp_state_t *ctcp_init(conn_t *conn, ctcp_config_t *cfg) {
  /* Connection could not be established. */
  if (conn == NULL) {
    return NULL;
  }

  /* Established a connection. Create a new state and update the linked list
     of connection states. */
  ctcp_state_t *state = calloc(sizeof(ctcp_state_t), 1);
  state->next = state_list;
  state->prev = &state_list;
  if (state_list)
    state_list->prev = &state->next;
  state_list = state;

  /* Set fields. */
  state->conn = conn;
  /* FIXME: Do any other initialization here. */
  state->seqno = 1; // MUST use 1 for CTCP
  state->ackno = 1;
  state->sent_segment_len = 0;
  state->sent_segment = NULL;
  state->recv_segment = NULL;
  state->recv_segment_len = 0;
  free(cfg);

  return state;
}

void ctcp_destroy(ctcp_state_t *state) {
  /* Update linked list. */
  if (state->next)
    state->next->prev = state->prev;

  *state->prev = state->next;
  conn_remove(state->conn);

  /* FIXME: Do any other cleanup here. */

  free(state);
  end_client();
}

void ctcp_read(ctcp_state_t *state) {
    /* FIXME */
    char *data = malloc(MAX_SEG_DATA_SIZE);
    if (!data) {
        fprintf(stderr, "Out of memory");
        exit(EXIT_FAILURE);
    }

    int data_len = conn_input(state->conn, data, MAX_SEG_DATA_SIZE);

    if (data_len == 0) {
        free(data);
        return;
    } else if (data_len == -1) {    // Error or EOF
        send_ctcp(state, FIN, NULL, 0);
        free(data);
        return;
    }

    send_ctcp(state, ACK, data, data_len);
    free(data);
}

void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
    /* FIXME */
    if (!valid_cksum(segment, len)) {
        free(segment);
        return;
    }
    size_t data_len = len - CTCP_HDR_SIZE;

    // Receive a ACK packet
    if ((segment->flags & TH_ACK) && (data_len == 0)) {
         //&& (state->seqno + state->data_len) == ntohl(segment->ackno)) {
        state->seqno = ntohl(segment->ackno);
        free(state->sent_segment);
        state->sent_segment = NULL;
        state->sent_segment_len = 0;
        free(segment);
        return;
    }

    // Receive a data packet
    if (segment->flags & TH_ACK && state->ackno == ntohl(segment->seqno)) {
        state->ackno = ntohl(segment->seqno) + data_len;
        send_ctcp(state, ACK, NULL, 0);
        state->recv_segment_len = len;
        state->recv_segment = segment;
        ctcp_output(state);
    }

    // Receive a FIN packet
    if (segment->flags & TH_FIN) {
        state->ackno++;
        send_ctcp(state, ACK, NULL, 0);
        conn_output(state->conn, NULL, 0);
        ctcp_destroy(state);
        free(segment);
        return;
    }

    free(segment);
}

void ctcp_output(ctcp_state_t *state) {
  /* FIXME */
    int data_len = state->recv_segment_len - CTCP_HDR_SIZE;
    if (conn_bufspace(state->conn) >= data_len) {
        conn_output(state->conn, state->recv_segment->data, data_len);
    }
}

void ctcp_timer() {
  /* FIXME */
    ctcp_state_t *state = state_list;
    for (; state != NULL; state = state->next) {
        // TODO: destroy when excess 6 times
        if (state->sent_segment != NULL) {
            conn_send(state->conn, state->sent_segment, state->sent_segment_len);
        }
    }
}
