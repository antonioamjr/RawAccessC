import com.sun.jna.Native;
 
public class TestaRawJNA {
 
  public static void main(String[] args) {
    RawJNA rawObj = (RawJNA)
      Native.loadLibrary("raw", RawJNA.class);
 
    /*String str = rawObj.readJNA(args[0], Integer.parseInt(args[1]), Long.parseLong(args[2]));
    System.out.println("A String retornada foi: " + str);

    if (Integer.parseInt(args[4]) >= 2 ){
      rawObj.writeJNA(args[0], args[3], Long.parseLong(args[2]));
      String str = rawObj.readJNA(args[0], Integer.parseInt(args[1]), Long.parseLong(args[2]));
      System.out.println("A String retornada foi: " + str);
    }*/

    int resultado = rawObj.main(args.length, args);
    System.out.println("O resultado é: " + resultado);
  }
}
