import argparse
import random

def main():
    parser = argparse.ArgumentParser()

    parser.add_argument('--num-cpus', '-c', default=32, type=int,
                        help='number of routers, default is 32')
    parser.add_argument('--mesh-rows', '-d', default=8, type=int,
                        help='number of rows in mesh network, default is 8')

    options = parser.parse_args()

    num_cpus = options.num_cpus
    num_rows = options.mesh_rows
    num_columns = num_rows
    num_routers = num_rows * num_columns

    active_routers = []
    # add the first two corners
    active_routers.append(0)
    active_routers.append(num_rows - 1)
    # add the last row
    for i in range(num_rows):
        active_routers.append(num_routers - i - 1)

    random.seed(99)
    remaining_cores = []
    for i in range(1, num_rows - 1):
        remaining_cores.append(i)
    for i in range(num_rows, num_routers - num_rows):
        remaining_cores.append(i)

    for i in range(num_cpus - 2 - num_rows):
        r = random.choice(remaining_cores)
        active_routers.append(r)
        remaining_cores.remove(r)

    active_routers.sort()

    off_cores = '{'
    off_cores += str(remaining_cores[0])
    for i , core in enumerate(remaining_cores[1:]):
        off_cores += ',{}'.format(core)
    off_cores += '}'
    print('attached routers : {}'.format(active_routers))
    print('off cores: {}'.format(off_cores))

if __name__ == '__main__':
    main()
