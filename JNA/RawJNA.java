import com.sun.jna.Library;
 
public interface RawJNA extends Library {
  public int main(int argc, String[] argv);
  public String readJNA(String device_name, int size, long offset);
  public boolean writeJNA(String device_name, String message, long offset);
}
