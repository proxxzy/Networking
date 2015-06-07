#include "sender.h"

void init_sender(Sender* sender, int id) {
  sender->send_id = id;
  sender->input_cmdlist_head = NULL;
  sender->input_framelist_head = NULL;
  sender->LFS = MAX_SEQ-1;
  sender->LAR = MAX_SEQ-1;
  sender->SWS = 1;
}

struct timeval *sender_get_next_expiring_timeval(Sender* sender) {
  return NULL;
}


void handle_acks(Sender* s, LLnode** frameHead) {
  LLnode *temp = NULL;
  char *inbound = NULL;
  while(ll_get_length(s->input_framelist_head) > 0 ) {
    temp = ll_pop_node(&s->input_framelist_head);
    inbound = (char *)temp->value;
   
    if( inbound[0] == s->send_id && (s->LAR+MAX_SEQ+1) % MAX_SEQ == inbound[2] ) {
      s->LAR = inbound[2]; 
      fprintf(stderr, "NOTICE <SND_%d> : Got an ACK from <RECV_%d>\n",s->send_id, inbound[1]);
    }
    free(inbound);
  }
 
}

#define SND_DEBUG 0
void handle_input( Sender* s, LLnode** frameHead ) {
  LLnode *temp = NULL;
  Cmd *input = NULL;

  while( ll_get_length(s->input_cmdlist_head) > 0 ) {
    // CHECK:  within SWS limits
    if( (s->LAR > s->LFS ? s->LFS + MAX_SEQ : s->LFS) - s->LAR < s->SWS ) {
      if(SND_DEBUG) printf("Sender is within SWS limits\n");
      s->LFS = (s->LFS + 1 + MAX_SEQ) % MAX_SEQ;
    } else {
      if(SND_DEBUG) printf("WARNING :: SWS exceeded\n");
      break; //break out to handle_ack()
    }
    temp = ll_pop_node(&s->input_cmdlist_head);
    input = (Cmd *)temp->value;
    free(temp);

    if( strlen(input->message) > MAX_FRAME_SIZE ) {
      printf("Can't send msg > %d yet\n", MAX_FRAME_SIZE);
      free(input->message);
      free(input);
      continue;
    } 
    Frame *sendFrame = (Frame *)malloc(sizeof(Frame));
    sendFrame->src = input->src_id;
    sendFrame->dst = input->dst_id;
    sendFrame->seq = s->LFS;
    sendFrame->flag = SEQ_FLAG;
    strcpy(sendFrame->data, input->message);
    sendFrame->crc = crc32(sendFrame, 4+FRAME_PAYLOAD_SIZE);
    char *serialized = convert_frame_to_char(sendFrame);


    ll_append_node(frameHead, serialized);
    free(input->message);
    free(input);
    free(sendFrame);
  }    
}


void handle_timedout_frames(Sender* sender, LLnode** outgoing_frames_head_ptr) {
}


void * run_sender(void * input_sender) {    
  struct timespec   time_spec;
  struct timeval    curr_timeval;
  const int WAIT_SEC_TIME = 0;
  const long WAIT_USEC_TIME = 100000;
  Sender * sender = (Sender *) input_sender;    
  LLnode * outgoing_frames_head;
  struct timeval * expiring_timeval;
  long sleep_usec_time, sleep_sec_time;
    
  pthread_cond_init(&sender->buffer_cv, NULL);
  pthread_mutex_init(&sender->buffer_mutex, NULL);

  while(1) {    
    outgoing_frames_head = NULL;

    //Get the current time
    gettimeofday(&curr_timeval, NULL);

    //time_spec is a data structure used to specify when the thread should wake up
    //The time is specified as an ABSOLUTE (meaning, conceptually, you specify 9/23/2010 @ 1pm, wakeup)
    time_spec.tv_sec  = curr_timeval.tv_sec;
    time_spec.tv_nsec = curr_timeval.tv_usec * 1000;

    //Check for the next event we should handle
    expiring_timeval = sender_get_next_expiring_timeval(sender);

    //Perform full on timeout
    if (expiring_timeval == NULL) {
      time_spec.tv_sec += WAIT_SEC_TIME;
      time_spec.tv_nsec += WAIT_USEC_TIME * 1000;
    } else {
      //Take the difference between the next event and the current time
      sleep_usec_time = timeval_usecdiff(&curr_timeval, expiring_timeval);

      //Sleep if the difference is positive
      if (sleep_usec_time > 0) {
        sleep_sec_time = sleep_usec_time/1000000;
        sleep_usec_time = sleep_usec_time % 1000000;   
        time_spec.tv_sec += sleep_sec_time;
        time_spec.tv_nsec += sleep_usec_time*1000;
      }   
    }

    //Check to make sure we didn't "overflow" the nanosecond field
    if (time_spec.tv_nsec >= 1000000000) {
      time_spec.tv_sec++;
      time_spec.tv_nsec -= 1000000000;
    }

        
    //*****************************************************************************************
    //NOTE: Anything that involves dequeing from the input frames or input commands should go 
    //      between the mutex lock and unlock, because other threads CAN/WILL access these structures
    //*****************************************************************************************
    pthread_mutex_lock(&sender->buffer_mutex);

    //Check whether anything has arrived
    int input_cmd_length = ll_get_length(sender->input_cmdlist_head);
    int inframe_queue_length = ll_get_length(sender->input_framelist_head);
        
    //Nothing (cmd nor incoming frame) has arrived, so do a timed wait on the sender's condition variable (releases lock)
    //A signal on the condition variable will wakeup the thread and reaquire the lock
    if (input_cmd_length == 0 && inframe_queue_length == 0) {
      pthread_cond_timedwait(&sender->buffer_cv, &sender->buffer_mutex, &time_spec);
    }
    //Implement this
    handle_acks(sender, &outgoing_frames_head);
    //Implement this
    handle_input(sender, &outgoing_frames_head);
    
    pthread_mutex_unlock(&sender->buffer_mutex);


    //Implement this
    handle_timedout_frames(sender, &outgoing_frames_head);

    //CHANGE THIS AT YOUR OWN RISK!
    //Send out all the frames
    int ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
        
    while(ll_outgoing_frame_length > 0) {
      LLnode * ll_outframe_node = ll_pop_node(&outgoing_frames_head);
      char * char_buf = (char *)  ll_outframe_node->value;

      //Don't worry about freeing the char_buf, the following function does that
      send_msg_to_receivers(char_buf);

      //Free up the ll_outframe_node
      free(ll_outframe_node);

      ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
    }
  }
  pthread_exit(NULL);
  return 0;
}
