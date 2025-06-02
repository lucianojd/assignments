#!/usr/bin/env python3
import argparse
from datetime import datetime
import random
import sys

parser = argparse.ArgumentParser(prog="customer_generator", description="Generate customer data")
parser.add_argument("-n", "--customer_num", type=int, default=10, help="Number of customers to generate")
parser.add_argument("-c", "--class_num", type=int, default=2, help="Number of customer classes")
parser.add_argument("-S", "--max_service", type=int, default=10, help="Maximum wait time in seconds")
parser.add_argument("-A", "--max_arrival", type=int, default=10, help="Maximum arrival time in seconds")
parser.add_argument("-s", "--min_service", type=int, default=1, help="Minimum service time in seconds")
parser.add_argument("-a", "--min_arrival", type=int, default=1, help="Minimum arrival time in seconds")
args = vars(parser.parse_args())

class Customer:
    def __init__(self, id, cls, arrival_t, service_t):
        self.id = id
        self.cls = cls
        self.arrival_t = arrival_t
        self.service_t = service_t
    def __repr__(self):
        return f"{self.id}:{self.cls},{self.arrival_t},{self.service_t}"    
    def __str__(self):
        return f"{self.id}:{self.cls},{self.arrival_t},{self.service_t}"

def main():
    random.seed(datetime.now().microsecond)

    customer_num = args["customer_num"]
    class_num = args["class_num"]

    customers = [
        Customer(
            id=i + 1,
            cls=random.randint(0, class_num-1),
            arrival_t=random.randint(args["min_arrival"], args["max_arrival"]),
            service_t=random.randint(args["min_service"], args["max_service"])
        )
        for i in range(customer_num)
    ]

    with open("customers.txt", "w") as f:
        for customer in customers:
            f.write(f"{repr(customer)}\n")

if __name__ == "__main__":
    main()