package com.hcd.hcdadmin;

import org.apache.helix.NotificationContext;
import org.apache.helix.api.State;
import org.apache.helix.api.StateTransitionHandlerFactory;
import org.apache.helix.api.TransitionHandler;
import org.apache.helix.api.id.PartitionId;
import org.apache.helix.api.id.StateModelDefId;
import org.apache.helix.model.Message;
import org.apache.helix.model.StateModelDefinition;
import org.apache.helix.participant.statemachine.StateModelInfo;
import org.apache.helix.participant.statemachine.Transition;
import org.apache.log4j.Logger;

/**
 * Created by shawn on 4/15/16.
 */
public class HcdDiskStateModelFactory extends StateTransitionHandlerFactory<TransitionHandler> {
  private static final Logger LOG = Logger.getLogger(HcdDiskStateModelFactory.class);

  // states
  private static final State SLAVE = State.from("SLAVE");
  private static final State OFFLINE = State.from("OFFLINE");
  private static final State MASTER = State.from("MASTER");
  private static final State DROPPED = State.from("DROPPED");

  public static final StateModelDefId STATE_MODEL_NAME = StateModelDefId.from("HcdDiskStateModel");

  public HcdAdmin hcdAdmin;

  // state transition delay in ms.
  int delay = 100;

  public HcdDiskStateModelFactory(HcdAdmin hcdAdmin,  int delay) {
    this.hcdAdmin = hcdAdmin;
    this.delay = delay;
  }

  public static StateModelDefinition defineStateModel() {
    StateModelDefinition.Builder builder = new StateModelDefinition.Builder(STATE_MODEL_NAME);
    // Add states and their rank to indicate priority. Lower the rank higher the
    // priority
    builder.addState(MASTER, 1);
    builder.addState(SLAVE, 2);
    builder.addState(OFFLINE);
    builder.addState(DROPPED);
    // Set the initial state when the node starts
    builder.initialState(OFFLINE);

    // Add transitions between the states.
    builder.addTransition(OFFLINE, SLAVE);
    builder.addTransition(SLAVE, OFFLINE);
    builder.addTransition(SLAVE, MASTER);
    builder.addTransition(MASTER, SLAVE);
    builder.addTransition(OFFLINE, DROPPED);

    // set constraints on states.
    // static constraint
    builder.upperBound(MASTER, 1);
    // dynamic constraint, R means it should be derived based on the replication
    // factor.
    builder.dynamicUpperBound(SLAVE, "R");

    StateModelDefinition statemodelDefinition = builder.build();
    return statemodelDefinition;
  }

  @Override
  public TransitionHandler createStateTransitionHandler(PartitionId partitionId) {
    HcdDiskStateModel model = new HcdDiskStateModel(hcdAdmin);
    model.setDelay(delay);
    return model;
  }

  @StateModelInfo(initialState = "OFFLINE", states = "{'OFFLINE','SLAVE','MASTER', 'DROPPED'}")
  public static class HcdDiskStateModel extends TransitionHandler {

    private int transDelay = 100;
    private HcdAdmin hcdAdmin;

    public void setDelay(int delay) {
      transDelay = delay;
    }

    public HcdDiskStateModel(HcdAdmin hcdAdmin) {
      this.hcdAdmin = hcdAdmin;
    }

    @Transition(from = "MASTER", to = "SLAVE")
    public void masterToSlave(Message message, NotificationContext context) {
      LOG.info(message.getTgtName() + " : " + message.getResourceId() + "/" + message.getPartitionId() +
          ": transit from MASTER to SLAVE");
      sleep();
    }

    @Transition(from = "OFFLINE", to = "SLAVE")
    public void offlineToSlave(Message message, NotificationContext context) {
      // TODO: allocate disk space, start local storage engine for this partition.
      String diskName = message.getTgtName();
      long freeDiskSpace = hcdAdmin.getDiskFreeSpace(diskName);
      // get LUN partition size.
      String lunName = message.getResourceId().stringify();
      String partitionName = message.getPartitionId().stringify();
      long partitionSize = hcdAdmin.getPartitionSize(lunName, partitionName);
      if (freeDiskSpace >= partitionSize) {
        freeDiskSpace -= partitionSize;
        hcdAdmin.setDiskFreeSpace(diskName, freeDiskSpace);
      } else {
        // failed.
      }
      LOG.info(message.getTgtName() + " : " + message.getResourceId() + "/" + message.getPartitionId() +
          ": transit from OFFLINE to SLAVE");
      sleep();
    }

    @Transition(from = "SLAVE", to = "OFFLINE")
    public void slaveToOffline(Message message, NotificationContext context) {
      LOG.info(message.getTgtName() + " : " + message.getResourceId() + "/" + message.getPartitionId() +
          ": transit from SLAVE to OFFLINE");
      sleep();
    }

    @Transition(from = "SLAVE", to = "MASTER")
    public void slaveToMaster(Message message, NotificationContext context) {
      LOG.info(message.getTgtName() + " : " + message.getResourceId() + "/" + message.getPartitionId() +
          ": transit from SLAVE to MASTER");
      sleep();
    }

    @Transition(from = "OFFLINE", to = "DROPPED")
    public void offlineToDropped(Message message, NotificationContext context) {
      LOG.info(message.getTgtName() + " : " + message.getResourceId() + "/" + message.getPartitionId() +
          ": transit from OFFLINE to DROPPED");
      sleep();
    }

    private void sleep() {
      try {
        Thread.sleep(transDelay);
      } catch (Exception e) {
        e.printStackTrace();
      }
    }

  }

}

