
import argparse
import os
import os.path
from glob import iglob


def file_has_2_lines_or_more(fname):
    with open(fname) as f:
        for i, l in enumerate(f):
            if i == 1:
                return True
        return False


def main():
    parser = argparse.ArgumentParser(description = 'Remove executions that have failed.')
    parser.add_argument('-i', '--input-dirs', nargs='+', required=True, help='Input data dirs.')
    parser.add_argument('-d', '--delete', action="store_true", help='If not specified only shows files that would be deleted.')
    args = parser.parse_args()

    for d in args.input_dirs:
        print("{}:".format(os.path.basename(os.path.abspath(d))))
        file_list = [f for f in iglob("{}/data/*fin*.csv".format(d), recursive=False) if os.path.isfile(f)]
        del_list = []
        for f in file_list:
            if not file_has_2_lines_or_more(f):
                del_list.append(f)
                del_list.append(f.replace("_fin.csv", "_tot.csv"))
                del_list.append(f.replace("_fin.csv", ".csv"))
        if len(del_list) > 0:
            for f in sorted(del_list):
                print("\t{}".format(f))
                if args.delete:
                    os.remove(f)


if __name__ == "__main__":
    main()
