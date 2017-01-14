/* 
 * File:   streamer.cpp
 * Author: EX4
 *
 * Created on January 14, 2017, 8:16 AM
 */

//standard libs
#include <cstdlib>

//for exception
#include <exception>

//this is for streaming array buffer
#include <vector>

//opencv lib
#include <opencv2/opencv.hpp>

//mongoose library
#include "mongoose.h"

//thread pool
#include "thpool.h"

//fps counter
#include "fps_counter.h"

//c++ namespace
using namespace std;
using namespace cv;

//global var
//server
static sig_atomic_t server_system_signal_received = 0;
static const char *server_http_port = "12345";
static struct mg_serve_http_opts server_http_opts;

//streaming buffer
std::vector<unsigned char> cam_streaming_buffer, cam_streaming_image_buffer,
cam_streaming_buffer_arr[4], cam_streaming_image_buffer_arr[4]; //fill this with a single jpeg image data
std::vector<int> cam_streaming_converting_param = {CV_IMWRITE_JPEG_QUALITY, 50}; //set jpg quality in percent
uint8_t is_stream_sended = 0;

/*
 * mongoose var
 */
struct mg_mgr mgr;
struct mg_connection *nc;

//thread pool
threadpool thpool = thpool_init(4);

//stream array
Mat rgb_img;

char flag_rgb_img_ready = 0;

/**
 * Signal handler for mongoose
 * @param sig_num
 */
static void system_signal_handler(int sig_num) {
    signal(sig_num, system_signal_handler); // Reinstantiate signal handler
    server_system_signal_received = sig_num;
}

/**
 * check nc is websocket or not
 * @param nc
 * @return 
 */
static int server_is_websocket_connection(const struct mg_connection *nc) {
    return nc->flags & MG_F_IS_WEBSOCKET;
}

/**
 * Check for image buffer mjpeg complete or not
 * @param aimg_buff
 * @return 
 */
static char is_image_buffer_complete(std::vector<unsigned char> aimg_buff) {
    return (aimg_buff.size() < 4 ||
            (aimg_buff[0] != 0xff && aimg_buff[0] != 0xd8) ||
            (aimg_buff[aimg_buff.size() - 2] != 0xff && aimg_buff[aimg_buff.size() - 1] != 0xd9)) ? 0 : 1;
}

static void stream_image_buffer_to_client(struct mg_connection *aconnection, std::vector<unsigned char> stream_buff) {
    //check for complete image buffer
    if (is_image_buffer_complete(stream_buff)) {
        mg_printf(aconnection, "--w00t\r\nContent-Type: image/jpeg\r\n"
                "Content-Length: %lu\r\n\r\n", (unsigned long) stream_buff.size());
        mg_send(aconnection, &stream_buff[0], stream_buff.size());
        mg_send(aconnection, "\r\n", 2);
        //                printf("IMAGE PUSHED TO %p\n", nc);
    }
}

static void push_frame_to_clients(struct mg_mgr *mgr) {
    struct mg_connection *nc;
    /*
     * mjpeg connections are tagged with the MG_F_USER_2 flag so we can find them
     * my scanning the connection list provided by the mongoose manager.
     */

    //send the buffer
    for (nc = mg_next(mgr, NULL); nc != NULL; nc = mg_next(mgr, nc)) {
        //got marked connection request
        //stream 1
        if (nc->flags & MG_F_USER_1) {
            stream_image_buffer_to_client(nc, cam_streaming_buffer_arr[0]);
        }

        //stream 2
        if (nc->flags & MG_F_USER_2) {
            stream_image_buffer_to_client(nc, cam_streaming_buffer_arr[1]);
        }

        //stream 3
        if (nc->flags & MG_F_USER_3) {
            stream_image_buffer_to_client(nc, cam_streaming_buffer_arr[2]);
        }

    }
}

/**
 * mongoose event handler
 * @param nc
 * @param ev
 * @param ev_data
 */
static void server_event_handler(struct mg_connection *nc, int ev, void *ev_data) {
    char payload_json[128] = {0};

    switch (ev) {
        case MG_EV_HTTP_REQUEST:
        {
            struct http_message *hm = (struct http_message *) ev_data;
            //original rgb image
            if (mg_vcmp(&hm->uri, "/stream/0") == 0) {
                nc->flags |= MG_F_USER_1; /* Set a mark on image requests */
                mg_printf(nc, "%s",
                        "HTTP/1.0 200 OK\r\n"
                        "Cache-Control: no-cache\r\n"
                        "Pragma: no-cache\r\n"
                        "Expires: Thu, 01 Dec 1994 16:00:00 GMT\r\n"
                        "Connection: close\r\n"
                        "Content-Type: multipart/x-mixed-replace; "
                        "boundary=w00t\r\n\r\n");
                printf("STREAM REQUEST ACCEPTED\n");
            }//edge image
            else if (mg_vcmp(&hm->uri, "/stream/1") == 0) {
                nc->flags |= MG_F_USER_2; /* Set a mark on image requests */
                mg_printf(nc, "%s",
                        "HTTP/1.0 200 OK\r\n"
                        "Cache-Control: no-cache\r\n"
                        "Pragma: no-cache\r\n"
                        "Expires: Thu, 01 Dec 1994 16:00:00 GMT\r\n"
                        "Connection: close\r\n"
                        "Content-Type: multipart/x-mixed-replace; "
                        "boundary=w00t\r\n\r\n");
                printf("STREAM REQUEST ACCEPTED\n");
            }//heatmap image
            else if (mg_vcmp(&hm->uri, "/stream/2") == 0) {
                nc->flags |= MG_F_USER_3; /* Set a mark on image requests */
                mg_printf(nc, "%s",
                        "HTTP/1.0 200 OK\r\n"
                        "Cache-Control: no-cache\r\n"
                        "Pragma: no-cache\r\n"
                        "Expires: Thu, 01 Dec 1994 16:00:00 GMT\r\n"
                        "Connection: close\r\n"
                        "Content-Type: multipart/x-mixed-replace; "
                        "boundary=w00t\r\n\r\n");
                printf("STREAM REQUEST ACCEPTED\n");
            } else {
                /* Usual HTTP request - serve static files */
                mg_serve_http(nc, hm, server_http_opts);
                //nc->flags |= MG_F_SEND_AND_CLOSE;
            }
            break;
        }
        case MG_EV_WEBSOCKET_HANDSHAKE_DONE:
        {
            /* New websocket connection. Tell everybody. */
            //            broadcast(nc, mg_mk_str("++ joined"));
            break;
        }
        case MG_EV_WEBSOCKET_FRAME:
        {
            struct websocket_message *wm = (struct websocket_message *) ev_data;
            /* New websocket message. Tell everybody. */
            //            struct mg_str d = {(char *) wm->data, wm->size};
            //            broadcast(nc, d);
            printf("Got data via WEBSOCKET : %.*s\n", (int) wm->size, wm->data);

            //parse command
            //            int cmd_id = -1;
            //            //cmd id,plyr ttl,set,serve id,score1,score2
            //            sscanf(wm->data, "%d,", &cmd_id);
            //            switch (cmd_id) {
            //                    //update board content
            //                case 0:
            //                {
            //                    char plyr_ttl[128] = {0};
            //                    int setx = 0;
            //                    int serve_id = 0;
            //                    int score1 = 0, score2 = 0;
            //
            //                    //parse more
            //                    sscanf(wm->data, "%d,%[^,],%d,%d,%d,%d", &cmd_id, plyr_ttl, &setx, &serve_id, &score1, &score2);
            //
            //                    //form to json                    
            //                    snprintf(payload_json, 128, "{\"cmd\":%d,\"player\":\"%s\",\"set\":%d,\"serve\":%d,\"scores\":[%d,%d]}",
            //                            cmd_id,
            //                            plyr_ttl,
            //                            setx,
            //                            (serve_id > 3 ? 3 : serve_id),
            //                            (score1 > 99 ? 99 : score1),
            //                            (score1 > 99 ? 99 : score2));
            //
            //                    //broadcast it
            //                    mg_ws_broadcast(nc, payload_json, strlen(payload_json), 0);
            //                    break;
            //                }
            //
            //                    //stop,start,reset timer
            //                case 1:
            //                {
            //                    int tmr_cmd = 0;
            //                    sscanf(wm->data, "%d,%d", &cmd_id, &tmr_cmd);
            //                    snprintf(payload_json, 128, "{\"cmd\":%d,\"tmr_mode\":%d}",
            //                            cmd_id, tmr_cmd);
            //                    //broadcast it
            //                    mg_ws_broadcast(nc, payload_json, strlen(payload_json), 0);
            //                    break;
            //                }
            //
            //                default:
            //                    break;
            //            }

            break;
        }
        case MG_EV_CLOSE:
        {
            /* Disconnect. Tell everybody. */
            //            if (is_websocket(nc)) {
            ////                broadcast(nc, mg_mk_str("-- left"));
            //            }
            break;
        }
        case MG_EV_POLL:
        {
            push_frame_to_clients(nc->mgr);
            break;
        }
    }
}

/**
 * Convert image to edge
 * @param rgb_in
 * @param edge_out
 */
void image_to_edge(cv::InputArray rgb_in, cv::OutputArray edge_out) {
    try {
        cvtColor(rgb_in, edge_out, COLOR_BGR2GRAY);
        GaussianBlur(edge_out, edge_out, Size(7, 7), 1.5, 1.5);
        Canny(edge_out, edge_out, 0, 30, 3);
    } catch (exception& err) {
        printf("ERROR : %s\n", err.what());
    }
}

/**
 * Convert image to heat map -- kind of....sort of....
 * @param rgb_in
 * @param map_out
 */
void image_to_heatmap(cv::InputArray rgb_in, cv::OutputArray map_out) {
    try {
        double min, max;
        cv::Mat adjMap, inMap = rgb_in.getMat();

        //calc max min pixel
        cv::minMaxIdx(inMap, &min, &max);

        // expand your range to 0..255. Similar to histEq();
        inMap.convertTo(adjMap, CV_8UC1, 255 / (max - min), -min);

        // this is great. It converts your grayscale image into a tone-mapped one, 
        // much more pleasing for the eye
        // function is found in contrib module, so include contrib.hpp 
        // and link accordingly
        //cv::Mat falseColorsMap;
        cv::applyColorMap(adjMap, map_out, cv::COLORMAP_JET);

        //FORGET TO RELEASE IT AND YOU ARE IN DEEEPPPPP SHIITTTTTTTTTTTTTT *_*
        inMap.release();
        adjMap.release();
    } catch (exception& err) {
        printf("ERROR : %s\n", err.what());
    }
}

void image_to_stream_buffer(cv::InputArray imgIn, std::vector<unsigned char> *stream_buff_out) {
    try {
        std::vector<unsigned char> img_vect;
        cv::imencode(".jpg", imgIn, img_vect, cam_streaming_converting_param);
        stream_buff_out->swap(img_vect);
    } catch (exception& err) {
        printf("ERROR : %s\n", err.what());
    }
}

/**
 * Do image processing here
 * @param ptr_data
 */
void task_image_processing(void* ptr_data) {
    //local var
    Mat edges;
    cv::Mat rgbMap;
    cv::Mat heatMap;

    while (server_system_signal_received == 0) {
        try {
            if (flag_rgb_img_ready) {

                //copy original image
                rgbMap = rgb_img;

                //perform edge detection
                image_to_edge(rgbMap, edges);

                //perform heatmap
                image_to_heatmap(rgbMap, heatMap);

                //save the stream
                //original image
                image_to_stream_buffer(rgbMap, &cam_streaming_buffer_arr[0]);

                //edge image
                image_to_stream_buffer(edges, &cam_streaming_buffer_arr[1]);

                //heatmap image
                image_to_stream_buffer(heatMap, &cam_streaming_buffer_arr[2]);

                //some fps counter
                printf("%f fps\n", fps_avg());
            }

            usleep(10000);
        } catch (exception& err) {
            printf("ERROR : %s\n", err.what());
        }
    }
}

/*
 * 
 */
int main(int argc, char** argv) {



    //set the webpage file
    system("echo \"<html><head></head><body><img src=\\\"/stream/0\\\"/><img src=\\\"/stream/1\\\"/><img src=\\\"/stream/2\\\"/></body></html>\" > /dev/shm/stream.html");

    /*
     signal handler
     */
    signal(SIGTERM, system_signal_handler);
    signal(SIGINT, system_signal_handler);
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    /*
     init mongoose manager
     */
    mg_mgr_init(&mgr, NULL);

    /*
     bind to port and doc root
     */
    nc = mg_bind(&mgr, server_http_port, server_event_handler);
    server_http_opts.document_root = "/dev/shm";
    server_http_opts.enable_directory_listing = "yes";
    mg_set_protocol_http_websocket(nc);
    printf("Started server on port %s\n", server_http_port);

    // Set up camera
    VideoCapture camera(0); //get 1st cam
    if (!camera.isOpened()) {
        return 1;
    }
    camera.set(CV_CAP_PROP_FRAME_WIDTH, 320);
    camera.set(CV_CAP_PROP_FRAME_HEIGHT, 240);

    //add work to pool thread
    thpool_add_work(thpool, task_image_processing, NULL);

    // All settings have been set, now go in endless loop and
    // take as many pictures you want..
    while (server_system_signal_received == 0) {
        //poll the server
        mg_mgr_poll(&mgr, 50);

        //grab the image
        camera >> rgb_img;

        //flag it
        flag_rgb_img_ready = 1;
    }

    /*
     free up mongoose manager
     */
    mg_mgr_free(&mgr);

    thpool_destroy(thpool);

    printf("\nTerminating server done\n");

    return 0;
}

