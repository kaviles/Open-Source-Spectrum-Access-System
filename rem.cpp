#include "rem.h"
#include "ui_rem.h"
#include <QToolButton>

Rem::Rem(QWidget *parent) :
  QMainWindow(parent),
  ui(new Ui::Rem)
{
  sem_init(&graphic_mutex,0,1);
  node_phores = new sem_t[node_num+1];
  for(int i = 0; i < node_num+1; i++){
      sem_init(&node_phores[i],0,0);
    }

  ui->setupUi(this);
  pthread_create(&listener_process, NULL, listener, (void *)this);
}

Rem::~Rem()
{
  app_open = false;
  pthread_join(listener_process,NULL);
  known_nodes.clear();
  sem_close(&graphic_mutex);
  delete ui;
}

void Rem::initializeNodeInfo(){
  config_t cfg, *cf;
  cf = &cfg;
  config_init(cf);
  int nodeID;


  if (!config_read_file(cf, "nodeInfo.cfg")) {
      fprintf(stderr, "%s:%d - %s\n",
              config_error_file(cf),
              config_error_line(cf),
              config_error_text(cf));
      config_destroy(cf);
      return ;
    }
  char node[20];
  for(int i = 1; i <= node_num; i++){
      sprintf(node,"Node%d.nodeID",i);
      if(config_lookup_int(cf,node,&nodeID)){
          nodeInfo n;
          n.nodeID = nodeID;
          sprintf(node,"Node%d.latitude",i);
          const char *latitude = NULL;
          config_lookup_string(cf, node, &latitude);

          std::string lat_str(latitude);
          n.latitude = lat_str;
          sprintf(node,"Node%d.longitude",i);
          const char *longitude = NULL;
          config_lookup_string(cf, node, &longitude);
          std::string lon_str(longitude);
          n.longitude = lon_str;

          sprintf(node,"Node%d",i);
          std::string visualID(node);
          n.visualID = visualID;
          known_nodes.push_back(n);

        }
    }
  printf("Known Node Size: %lu\n",known_nodes.size());

  for(unsigned int i = 0; i < known_nodes.size(); i++){
      std::cout << known_nodes[i].nodeID << std::endl;
    }
}

void *listener(void *_arg){
  Rem *rem = (Rem *)_arg;
  rem->initializeNodeInfo();
  struct sockaddr_in udp_server_addr;
  struct sockaddr_in udp_client_addr;
  socklen_t addr_len = sizeof(udp_server_addr);
  memset(&udp_server_addr, 0, addr_len);
  udp_server_addr.sin_family = AF_INET;
  udp_server_addr.sin_addr.s_addr = inet_addr("0.0.0.0");
  udp_server_addr.sin_port = htons(4680);
  int udp_server_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  int status = bind(udp_server_sock, (sockaddr *)&udp_server_addr, addr_len);
  fd_set read_fds;
  int recv_buffer_len = 2000;
  char recv_buffer[recv_buffer_len];
  std::cout << "Here\n";
  if(status < 0){
      std::cout << "Binding Failure" << std::endl;
    }else {
      while(rem->app_open){
          FD_ZERO(&read_fds);
          FD_SET(udp_server_sock, &read_fds);
          struct timeval timeout;
          timeout.tv_sec = 0;
          timeout.tv_usec = 1000;
          if(select(udp_server_sock + 1, &read_fds, NULL, NULL, &timeout) > 0){
              int recv_len = recvfrom(udp_server_sock, recv_buffer, recv_buffer_len, 0,
                                      (struct sockaddr *)&udp_client_addr, &addr_len);
              if(recv_len > 0){
                  //pthread_mutex_lock(&rem->listener_mutex);
                  rem->analyzeInfo(recv_buffer,recv_buffer_len);
                  // pthread_mutex_lock(&rem->listener_mutex);
                }
            }
        }
    }

  close(udp_server_sock);
  pthread_exit(0);
}


void Rem::analyzeInfo(const char *recv_buffer, int recv_len){
  std::string received_string;
  if (recv_len > 0){
      for (int i = 0; i < recv_len; i++)
        {
          received_string.push_back(recv_buffer[i]);
        }
    }
  pmt::pmt_t received_dict = pmt::deserialize_str(received_string);
  pmt::pmt_t not_found = pmt::mp(-1);
  pmt::pmt_t type = pmt::dict_ref(received_dict, pmt::string_to_symbol("type"), not_found);
  if(type!=not_found){
      pmt::pmt_t nodeID = pmt::dict_ref(received_dict, pmt::string_to_symbol("nodeID"), not_found);
      std::string type_str = pmt::symbol_to_string(type);
      std::cout << "Type : " << type_str.c_str() << std::endl;

      if(strcmp(type_str.c_str(),"SENSOR") == 0){
          std::cout << "SENSOR INFO\n";
          if(nodeID!=not_found){
              int nodeTemp = pmt::to_long(nodeID);
              int status = getNodePos(nodeTemp);
              if(status==-1){
                  printf("new node: %d",nodeTemp);
                  nodeInfo n;
                  n.nodeID = nodeTemp;
                  n.type = SENSOR;
                  known_nodes.push_back(n);
                } else{
                  known_nodes.at(status).type = SENSOR;
                  // create a function to get associated button
                  double occ = pmt::to_double(pmt::dict_ref(received_dict, pmt::string_to_symbol("occ"), not_found));
                  double lowerFreq = pmt::to_double(pmt::dict_ref(received_dict, pmt::string_to_symbol("lowerFreq"), not_found));
                  double bandwidth = pmt::to_double(pmt::dict_ref(received_dict, pmt::string_to_symbol("bandwidth"), not_found));
                  organizeData(status,occ,lowerFreq,bandwidth);
                }
            } else{
              std::cout << "Missing Node ID" << std::endl;
            }
        } else if(strcmp(type_str.c_str(),"SU") == 0){
          if(nodeID!=not_found){
              std::cout << "SU INFO\n";
              short unsigned int nodeTemp = pmt::to_long(nodeID);
              int status = getNodePos(nodeTemp);
              if(status==-1){
                  printf("new node: %d",nodeTemp);
                  nodeInfo n;
                  n.nodeID = nodeTemp;
                  n.type = SU;
                  known_nodes.push_back(n);

                } else{
                  if(known_nodes.at(status).type!=SU){
                      int sval;
                      known_nodes.at(status).type = SU;
                      sem_getvalue(&graphic_mutex, &sval);
                      std::cout << "Graphics Sem Value: " << sval << "\n";
                      if(sval >= 1){
                          sval = sem_trywait(&graphic_mutex);
                          if(sval >=0){
                              QCoreApplication::postEvent(this, new QEvent(QEvent::UpdateRequest),
                                                          Qt::LowEventPriority);
                              QMetaObject::invokeMethod(this, "updateVisualNode", Q_ARG(int,status));
                            }

                        }
                    }




                  pmt::pmt_t thru = pmt::dict_ref(received_dict, pmt::string_to_symbol("throughput"), not_found);
                  if(thru!=not_found){
                      known_nodes.at(status).tx_info.tx_freq = pmt::to_double(pmt::dict_ref(received_dict, pmt::string_to_symbol("tx_freq"), not_found));
                      known_nodes.at(status).tx_info.rx_freq = pmt::to_double(pmt::dict_ref(received_dict, pmt::string_to_symbol("rx_freq"), not_found));
                      performanceStats stats;
                      stats.bitrate = pmt::to_double(pmt::dict_ref(received_dict, pmt::string_to_symbol("throughput"), not_found));
                      stats.per = pmt::to_double(pmt::dict_ref(received_dict, pmt::string_to_symbol("per"), not_found));
                      if (known_nodes.at(status).tx_info.stats.size() > 10){
                          known_nodes.at(status).tx_info.stats.pop();
                        }
                      known_nodes.at(status).tx_info.stats.push(stats);
                      known_nodes.at(status).tx_info.state = "GRANT ACCEPTED";
                    }
                  pmt::pmt_t su_state =   pmt::dict_ref(received_dict, pmt::string_to_symbol("state"), not_found);

                  if(su_state!=not_found){

                      pmt::pmt_t state_pmt = pmt::dict_ref(received_dict, pmt::string_to_symbol("state"), not_found);
                      if(state_pmt!=not_found){
                          known_nodes.at(status).tx_info.state = pmt::symbol_to_string(state_pmt);
                        }


                    }
                  pmt::pmt_t group = pmt::dict_ref(received_dict, pmt::string_to_symbol("group"), not_found);

                  if (group!=not_found){
                      std::vector<short unsigned int> value_vector =  pmt::u16vector_elements (group);
                      known_nodes.at(status).tx_info.group = value_vector;
                      std::cout << "Group Found\n";
                      //pmt::pmt_t values = pmt::dict_values (group);

                    }
                  int sval;
                  updateGroupee(nodeTemp,status);
                  sem_getvalue(&graphic_mutex, &sval);
                  std::cout << "Graphics Sem Value: " << sval << "\n";
                  if(sval >= 1){
                      sval = sem_trywait(&graphic_mutex);
                      if(sval >= 0){
                          QCoreApplication::postEvent(this, new QEvent(QEvent::UpdateRequest),
                                                      Qt::LowEventPriority);
                          QMetaObject::invokeMethod(this, "updateVisualNode", Q_ARG(int,status));
                        }

                   }

                }
            } else{
              std::cout << "Missing Node ID" << std::endl;
            }

        } else if(strcmp(type_str.c_str(),"SAS") == 0){

        } else if(strcmp(type_str.c_str(),"PU") == 0){

        }

    }

  std::cout << received_dict << std::endl;
}

void Rem::updateGroupee(short unsigned int nodeTemp, int status){
  std::cout << "Updating Groupee\n";
  std::vector<short unsigned int> value_vector = known_nodes.at(status).tx_info.group;
  for(unsigned int i  = 0; i < value_vector.size(); i++){
      if (nodeTemp!=value_vector[i]){
          int pos = getNodePos(value_vector[i]);
          std::cout << "Groupee Pos: "<< pos <<"\n";
          if(pos > 0){
              if(known_nodes.at(pos).type!=SU){
                  known_nodes.at(pos).type = SU;
                  int sval;
                  sem_getvalue(&graphic_mutex, &sval);
                  std::cout << "Graphics Sem Value: " << sval << "\n";
                  if(sval >= 1){
                      sval = sem_trywait(&graphic_mutex);
                      if(sval >= 0){
                          QCoreApplication::postEvent(this, new QEvent(QEvent::UpdateRequest),
                                                      Qt::LowEventPriority);
                          QMetaObject::invokeMethod(this, "updateVisualNode", Q_ARG(int,pos));
                        }

                    }

                  //updateVisualNode(pos);

                }

              known_nodes.at(pos).tx_info = known_nodes.at(status).tx_info;
            }
        }
    }
}

bool Rem::channelSort(Rem::channelInfo x, Rem::channelInfo y){
  return (x.lowFrequency < y.lowFrequency);
}

void Rem::organizeData(int nodePos, double occ, double lowFreq,double bandwidth) {
  lowFreq = round(lowFreq/1e6)*1e6;
  int pos = getChannelPos(known_nodes[nodePos],lowFreq);

  int sval;
  sem_getvalue(&node_phores[nodePos], &sval);
  std::cout << "Semaphore Value for " << nodePos << " " << sval << "\n";
  bool info_update = true;
  if(sval > 0 && nodePos !=-1){
      sem_trywait(&node_phores[nodePos]);
      if(sval > 0){

          info_update = false;
        }
    }

  if(info_update){
      if(pos == -1){

          channelInfo c;
          c.lowFrequency = lowFreq;
          c.occ = occ;
          known_nodes[nodePos].channels.push_back(c);
          using namespace std::placeholders;
          std::sort(known_nodes[nodePos].channels.begin(),known_nodes[nodePos].channels.end(),std::bind(&Rem::channelSort,this,_1,_2));
          std::cout << "New Channel: " << lowFreq << std::endl;
        } else{

          known_nodes[nodePos].current_channel = pos;
          known_nodes[nodePos].channels[pos].lowFrequency = lowFreq;
          known_nodes[nodePos].channels[pos].bandwidth = bandwidth;
          known_nodes[nodePos].channels[pos].highFrequency = lowFreq + bandwidth;
          known_nodes[nodePos].channels[pos].occ = occ;
          known_nodes[nodePos].channels[pos].occ_history.push_back(occ);
          if(known_nodes[nodePos].channels[pos].occ_history.size() > 20){
              known_nodes[nodePos].channels[pos].occ_history.pop_front();
            }
          sem_getvalue(&graphic_mutex, &sval);
          std::cout << "Graphics Sem Value: " << sval << "\n";
          if(sval >= 1){
              sem_wait(&graphic_mutex);
              QCoreApplication::postEvent(this, new QEvent(QEvent::UpdateRequest),
                                          Qt::HighEventPriority);
              QMetaObject::invokeMethod(this, "updateVisualNode", Q_ARG(int,nodePos));
             //updateVisualNode(nodePos);
            }

          std::cout << "Node: " << nodePos << " " << known_nodes[nodePos].channels[pos].lowFrequency << std::endl;
          std::cout << "Node: " << nodePos << " " << known_nodes[nodePos].channels[pos].occ << std::endl;

        }
      sem_post(&node_phores[nodePos]);
    }


}


void Rem::updateVisualNode(int nodePos){

  if (known_nodes[nodePos].type == SENSOR){
      QString below_threshold("background-color: rgb(0,255,0)");
      QString above_threshold("background-color: red");

      QString visualID(known_nodes[nodePos].visualID.c_str());

      QToolButton *button = ui->centralWidget->findChild<QToolButton *>(visualID);

      bool detected = false;
      for(unsigned int i = 0; i < known_nodes[nodePos].channels.size(); i++){
          if(known_nodes[nodePos].channels[i].occ > occ_threshold){
              detected = true;
              /* for(unsigned int i = 0; i < known_nodes[nodePos].channels.size(); i++){
                  std::cout << " | LF" << known_nodes[nodePos].channels[i].lowFrequency << " occ: " << known_nodes[nodePos].channels[i].occ;
                }
              std::cout << "\n"; */
              break;
            }
        }

      if(detected){
          if(strcmp(known_nodes[nodePos].node_color.c_str(),"red") != 0){
              QCoreApplication::postEvent(button, new QEvent(QEvent::UpdateRequest),
                                          Qt::HighEventPriority);
              QMetaObject::invokeMethod(button, "setStyleSheet", Q_ARG(QString, above_threshold));
              known_nodes[nodePos].node_color = "red";
              printf("Updating to red for node %d\n",nodePos);
            }

        } else{
          if(strcmp(known_nodes[nodePos].node_color.c_str(),"green") != 0){
              QCoreApplication::postEvent(button, new QEvent(QEvent::UpdateRequest),
                                          Qt::HighEventPriority);
              QMetaObject::invokeMethod(button, "setStyleSheet", Q_ARG(QString, below_threshold));
              known_nodes[nodePos].node_color = "green";
              printf("Updating to green for node %d\n",nodePos);
            }
        }

    } else if (known_nodes[nodePos].type == SU){
      QString su_color("background-color: blue");
      std::string new_color  = "";
      std::cout << "Current Status: " << known_nodes.at(nodePos).tx_info.state.c_str() <<"\n";
      if (strcmp(known_nodes.at(nodePos).tx_info.state.c_str(),"GRANT ACCEPTED")==0){
          QString temp_color("background-color: rgb(0,0,255)");
          su_color = temp_color;
          new_color = "blue";
        } else{
          QString temp_color("background-color: rgb(255,255,0)");
          su_color = temp_color;
          new_color = "yellow";
        }

      if(strcmp(new_color.c_str(), known_nodes[nodePos].node_color.c_str()) !=0){
          QString visualID(known_nodes[nodePos].visualID.c_str());

          QToolButton *button = ui->centralWidget->findChild<QToolButton *>(visualID);

          QCoreApplication::postEvent(button, new QEvent(QEvent::UpdateRequest),
                                      Qt::HighEventPriority);
          QMetaObject::invokeMethod(button, "setStyleSheet", Q_ARG(QString, su_color));

          known_nodes[nodePos].node_color = new_color;

        }


    }

    sem_post(&graphic_mutex);

}
// TODO: Efficient search needed
int Rem::getNodePos(int nodeID)
{
  for (unsigned int i = 0; i < known_nodes.size(); i++)
    {
      if (nodeID == known_nodes[i].nodeID)
        {
          return i;
        }
    }
  return -1;
}
// TODO: Efficient search needed
int Rem::getChannelPos(nodeInfo n,double lowFreq)
{
  for (unsigned int i = 0; i < n.channels.size(); i++)
    {
      if (lowFreq == n.channels[i].lowFrequency)
        {
          return i;
        }
    }
  return -1;
}

// TODO: Efficient search needed
int Rem::getNodePos(std::string visualID)
{
  for (unsigned int i = 0; i < known_nodes.size(); i++)
    {
      if (visualID == known_nodes[i].visualID)
        {
          return i;
        }
    }
  return -1;
}


void Rem::showMoreInfo(std::string visualID){
  int pos = getNodePos(visualID);
  if(known_nodes.at(pos).type == SENSOR){
      SensorView s(pos, this);
      s.setModal(true);
      s.exec();
    } else if (known_nodes.at(pos).type == SU){
      SuView s(pos, this);
      s.setModal(true);
      s.exec();
    }

}

void Rem::on_Node6_clicked()
{
  showMoreInfo("Node6");
}
void Rem::on_Node11_clicked()
{
  showMoreInfo("Node11");
}


void Rem::on_Node12_clicked()
{
  showMoreInfo("Node12");
}

void Rem::on_Node10_clicked()
{
  showMoreInfo("Node10");
}

void Rem::on_Node8_clicked()
{
  showMoreInfo("Node8");
}

void Rem::on_Node5_clicked()
{
  showMoreInfo("Node5");
}

void Rem::on_Node3_clicked()
{
  showMoreInfo("Node3");
}

void Rem::on_Node2_clicked()
{
  showMoreInfo("Node2");
}

void Rem::on_Node1_clicked()
{
  showMoreInfo("Node1");
}

void Rem::on_Node4_clicked()
{
  showMoreInfo("Node4");
}

void Rem::on_Node7_clicked()
{
  showMoreInfo("Node7");
}

void Rem::on_Node9_clicked()
{
  showMoreInfo("Node9");
}