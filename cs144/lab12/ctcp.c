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
  uint32_t seqno;
  uint32_t ackno;
  int data_len;
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
    segment->window = MAX_SEG_DATA_SIZE;
    segment->cksum = 0;
    if (len > 0) {
        memcpy(segment->data, data, len);
    }
    segment->cksum = cksum(segment, segment_len);
    state->data_len = len;
    return conn_send(state->conn, segment, segment_len);
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
  state->seqno = 1;
  state->ackno = 1;
  state->data_len = 0;
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
        //ctcp_destroy(state);
        free(data);
        return;
    }

    send_ctcp(state, ACK, data, data_len);
    free(data);
}

void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
    /* FIXME */
    // Just an ACK packet
    size_t data_len = len - CTCP_HDR_SIZE;
    if ((segment->flags & htonl(ACK)) && (data_len == 0)) {
         //&& (state->seqno + state->data_len) == ntohl(segment->ackno)) {
        state->seqno = ntohl(segment->ackno);
        free(segment);
        return;
    }

    // Handle duplicated segments
    /*
    if ((ntohl(segment->seqno) + data_len) < state->ackno) {
        free(segment);
        return;
    }
    */

    state->ackno = ntohl(segment->seqno) + data_len;
    send_ctcp(state, ACK, NULL, 0);
    ctcp_output(state);

    if (segment->flags & ntohl(FIN)) {
        conn_output(state->conn, NULL, 0);
        ctcp_destroy(state);
        return;
    }

    conn_output(state->conn, segment->data, data_len);

    free(segment);
}

void ctcp_output(ctcp_state_t *state) {
  /* FIXME */
}

void ctcp_timer() {
  /* FIXME */
}
