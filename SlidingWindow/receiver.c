#include "receiver.h"

void init_receiver(Receiver * r, int id) {
  r->recv_id = id;
  r->input_framelist_head = NULL;
  r->LAF = MAX_RWS;
  r->LFR = -1;
  r->RWS = 0;

  r->buffer = (char **) malloc( MAX_RWS );
}


//TODO: Suggested steps for handling incoming frames
//    1) Dequeue the Frame from the sender->input_framelist_head
//    2) Convert the char * buffer to a Frame data type
//    3) Check whether the frame is corrupted
//    4) Check whether the frame is for this receiver
//    5) Do sliding window protocol for sender/receiver pair


void handle_incoming_msgs(Receiver *r, LLnode **output_framelist_head) {
  while( ll_get_length(r->input_framelist_head) > 0 ) {
    LLnode *msgNode = ll_pop_node(&r->input_framelist_head);
    char *raw_buf = (char *)msgNode->value;
    Frame *inframe = convert_char_to_frame(raw_buf);

    // If corrupted, drop this frame 
    if( !(crc32(inframe, 4+FRAME_PAYLOAD_SIZE) ^ inframe->crc) ) {
      // if not the right recipient, drop this frame
      if( r->recv_id == inframe->dst ) {
        printf("<RECV_%d>:[%s]\n", r->recv_id, inframe->data);
        char *ack_buf = (char *)malloc(MAX_FRAME_SIZE);
        memcpy(ack_buf, raw_buf, MAX_FRAME_SIZE);
        ack_buf[3] = ACK_FLAG;
        ll_append_node(output_framelist_head, ack_buf);
      }
    }

    free(msgNode);
    free(raw_buf);
    free(inframe);
  }
}

void * run_receiver(void * input_receiver) {    
    struct timespec   time_spec;
    struct timeval    curr_timeval;
    const int WAIT_SEC_TIME = 0;
    const long WAIT_USEC_TIME = 100000;
    Receiver * receiver = (Receiver *) input_receiver;
    LLnode * outgoing_frames_head;


    //This incomplete receiver thread, at a high level, loops as follows:
    //1. Determine the next time the thread should wake up if there is nothing in the incoming queue(s)
    //2. Grab the mutex protecting the input_msg queue
    //3. Dequeues messages from the input_msg queue and prints them
    //4. Releases the lock
    //5. Sends out any outgoing messages

    pthread_cond_init(&receiver->buffer_cv, NULL);
    pthread_mutex_init(&receiver->buffer_mutex, NULL);

    while(1) {    
        //NOTE: Add outgoing messages to the outgoing_frames_head pointer
        outgoing_frames_head = NULL;
        gettimeofday(&curr_timeval, NULL);

        //Either timeout or get woken up because you've received a datagram
        //NOTE: You don't really need to do anything here, but it might be useful for debugging purposes to have the receivers periodically wakeup and print info
        time_spec.tv_sec  = curr_timeval.tv_sec;
        time_spec.tv_nsec = curr_timeval.tv_usec * 1000;
        time_spec.tv_sec += WAIT_SEC_TIME;
        time_spec.tv_nsec += WAIT_USEC_TIME * 1000;
        if (time_spec.tv_nsec >= 1000000000) {
            time_spec.tv_sec++;
            time_spec.tv_nsec -= 1000000000;
        }

        //*****************************************************************************************
        //NOTE: Anything that involves dequeing from the input frames should go 
        //      between the mutex lock and unlock, because other threads CAN/WILL access these structures
        //*****************************************************************************************
        pthread_mutex_lock(&receiver->buffer_mutex);

        //Check whether anything arrived
        int incoming_msgs_length = ll_get_length(receiver->input_framelist_head);
        if (incoming_msgs_length == 0) {
            //Nothing has arrived, do a timed wait on the condition variable (which releases the mutex). Again, you don't really need to do the timed wait.
            //A signal on the condition variable will wake up the thread and reacquire the lock
            pthread_cond_timedwait(&receiver->buffer_cv, &receiver->buffer_mutex, &time_spec);
        }
        handle_incoming_msgs(receiver, &outgoing_frames_head);
        pthread_mutex_unlock(&receiver->buffer_mutex);
        
        //CHANGE THIS AT YOUR OWN RISK!
        //Send out all the frames user has appended to the outgoing_frames list
        int ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
        while(ll_outgoing_frame_length > 0) {
            LLnode * ll_outframe_node = ll_pop_node(&outgoing_frames_head);
            char * char_buf = (char *) ll_outframe_node->value;

            //The following function frees the memory for the char_buf object
            send_msg_to_senders(char_buf);

            //Free up the ll_outframe_node
            free(ll_outframe_node);

            ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
        }
    }
    pthread_exit(NULL);

}
