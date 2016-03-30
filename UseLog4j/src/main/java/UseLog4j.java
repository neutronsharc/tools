/**
 * Created by shawn on 3/29/16.
 */


public class UseLog4j {
  private static final org.apache.log4j.Logger LOG =
      org.apache.log4j.Logger.getLogger(UseLog4j.class);

  public static void main(String[] args) throws Exception {

    LOG.info("got " + args.length + " params");

    for (String s : args) {
      LOG.info("arg: " + s);
    }

  }

}
